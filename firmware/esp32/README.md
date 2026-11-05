# ESP32 firmware

Target: original ESP32-WROOM-32 / ESP32 DevKit V1. The firmware exposes one composite BLE HID peripheral containing keyboard and mouse reports. This is BLE HID, not native USB HID.

## Build

    cp .env.example .env
    # Set ESP32_API_SECRET and ESP32_AP_PASSWORD in .env.
    pnpm install
    pnpm firmware:build
    pnpm firmware:upload
    pnpm --filter @esp32-hid/firmware monitor

Install pioarduino PlatformIO Core 6.2.0+ before running PlatformIO. EspBle requires Arduino-ESP32 3.2.0+ on original ESP32. The PlatformIO environment uses the pioarduino platform release.

The root .env is converted to ignored include/secrets.h during build. Never commit generated credentials.

## Interfaces
- HTTP API: port 80
- WebSocket event stream: port 81
- Serial CLI: 115200 baud
- Default AP: ESP32-HID at 192.168.4.1

## Composite HID
EspBle configures keyboard and mouse reports into one BLE HID service. Pairing is initiated from the host OS. After a disconnect, firmware restarts advertising; the host decides whether and when to reconnect. One active host is the initial target.

## Macro recording
Recording stores only commands deliberately sent through this controller API or Serial CLI. It does not capture the paired computer's physical keyboard. Macro steps are stored in NVS with bounded action and name limits.

## Limits
- Mouse movement is relative and each x/y delta is clamped to ±100 counts.
- Text typing is ASCII-focused; Unicode and layout handling depend on the host keyboard layout.
- Queue and macro sizes are bounded.
- Remote telemetry requires an HTTPS endpoint and trusted CA certificate; its offline queue is RAM-only.
- Physical hardware tests are still required for BLE, Wi-Fi coexistence and host reconnection.

## Secure OTA

See ../../docs/ota.md for signed HTTPS manifest generation, trust-anchor configuration and update API. OTA is disabled unless both a trusted CA and an ECDSA public key are compiled into the image. See ../../docs/hardware-validation.md for actual BLE, reconnect, queue and NVS acceptance tests.
