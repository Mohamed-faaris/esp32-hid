# Secure OTA update

Secure OTA is isolated on the `feature/secure-ota` branch and its own pull request. It is disabled by default until both the HTTPS CA bundle and the ECDSA public key are configured in the firmware build environment.

## Trust model

- The device fetches the manifest and image only over HTTPS using the configured CA certificate. Redirects are disabled.
- A release manifest is signed with ECDSA/SHA-256. The verifier uses the public key compiled into the device; the private key must remain offline.
- The signature binds the schema, target board, semantic version, security counter, release channel, HTTPS image URL, exact image size and SHA-256 digest.
- The device rejects wrong-board/channel manifests, non-HTTPS URLs, invalid signatures, malformed versions, older versions, non-increasing releases, insufficient security counters, oversized images and hash mismatches.
- The image is written to the inactive OTA slot. The command queue must be idle, not paused, and macro recording/replay must be stopped. HID state is released before the update.
- After reboot, firmware startup checks confirm the pending image or select the saved previous app partition.

**Important limits:** the security counter is stored in NVS, not in an eFuse. An attacker with physical flash access may erase or alter NVS. This project does not configure Secure Boot V2 or flash encryption, so this implementation does not claim protection against physical firmware replacement or flash extraction. First-boot rollback behavior requires a compatible bootloader and must be verified on the exact board/bootloader. Do not use OTA in production until interrupted-power, bad-signature and rollback tests pass on hardware.

## Configure the release trust anchors

Create a P-256 ECDSA key pair on a trusted release machine. Keep the private key offline and never commit or put it in device `.env`:

```sh
openssl ecparam -name prime256v1 -genkey -noout -out ota-private.pem
openssl ec -in ota-private.pem -pubout -out ota-public.pem
```

Set these in root `.env`:

- `ESP32_OTA_ROOT_CA`: PEM CA certificate that validates the HTTPS manifest and image endpoints.
- `ESP32_OTA_PUBLIC_KEY`: the PEM public key from `ota-public.pem`.

The build script escapes PEM newlines and writes these into an ignored `secrets.h`. Never put `ota-private.pem` in the device image, repository or root `.env`.

## Build and publish a release

Build a firmware binary with the intended version and security counter in `firmware/esp32/include/config.h`. Publish the binary on an HTTPS origin trusted by the configured CA. Generate the manifest:

```sh
python3 scripts/create_ota_manifest.py \
  --image firmware/esp32/.pio/build/esp32dev/firmware.bin \
  --version 1.1.0 \
  --security-version 2 \
  --image-url https://updates.example.com/esp32-hid/firmware-1.1.0.bin \
  --private-key /secure/offline/ota-private.pem \
  --output manifest.json
```

Upload `manifest.json` to an HTTPS URL. The manifest URL is the value sent to the device. The public key embedded in the running firmware must match the private signing key used by the release process.

Run unit tests for the release tooling:

```sh
python3 -m unittest discover -s scripts -p 'test_*.py'
```

## Start an update

The device needs a working station connection to the Internet; AP-only mode cannot fetch releases from the network. First check `GET /api/ota/status`, then send the authenticated request:

```sh
curl --fail-with-body \
  -H "Authorization: Bearer $ESP32_API_SECRET" \
  -H "Content-Type: application/json" \
  -d '{"manifest_url":"https://updates.example.com/esp32-hid/manifest.json"}' \
  "$ESP32_API_URL/api/ota/update"
```

The API returns HTTP 202 when the worker starts. Poll `GET /api/ota/status` for its state. Commands are rejected while the updater runs.

## Required real-board release checks

- [ ] Valid signed update, version and image hash.
- [ ] Tampered manifest, wrong signature and mismatched hash rejected.
- [ ] HTTP URLs, redirects, wrong board/channel and oversized image rejected.
- [ ] Downgrade and lower security counter rejected.
- [ ] Interrupted download and power loss do not destroy the running slot.
- [ ] New image passes startup confirmation; failed startup rolls back to the prior app.
- [ ] Host can reconnect BLE HID after a successful update and after rollback.

The OTA workflow is not production-approved until every item above has been tested on the exact WROOM-32 board and bootloader.
