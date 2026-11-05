# ESP32 HID Controller

BLE HID keyboard controller for the original ESP32 / ESP32-WROOM-32. This firmware uses BLE HID (not USB HID) and exposes a local HTTP API, WebSocket log stream, and Serial CLI.

## Implemented in main

- BLE keyboard initialization, text typing, key press, hotkey, and release-all on stop.
- Wi-Fi AP + optional STA using settings in include/secrets.h.
- Authenticated HTTP command routes and a shared bounded sequential queue.
- Queue pause, resume, and stop/clear.
- Typing delay, bounded jitter, sleep commands, and sequences.
- NVS persistence for typing configuration and log level.
- Serial CLI for status, queue controls, typing, key and hotkey commands.
- Serial and WebSocket logs.
- Optional HTTPS telemetry worker with a bounded RAM queue and bounded retries. Configure a valid root CA in HID_TELEMETRY_ROOT_CA; telemetry remains disabled without valid TLS configuration.

## Hardware and Bluetooth behaviour

Target board: ESP32-WROOM-32 (original ESP32). The board supports BLE but has no native USB device HID.

The ESP32 operates as a BLE HID peripheral. Pairing, reconnecting, and forgetting the device are performed from the computer's Bluetooth settings. The ESP32 cannot command a remote computer to reconnect or inspect its saved paired-device list. One active host is the supported target.

## Setup

1. Install PlatformIO.
2. Copy include/secrets.example.h to include/secrets.h.
3. Set a long random HID_API_SECRET and a strong HID_AP_PASSWORD.
4. Optionally configure station Wi-Fi credentials.
5. For remote telemetry, configure HID_TELEMETRY_ROOT_CA with the PEM root CA of the HTTPS API.
6. Build and flash:

```sh
pio run -e esp32dev
pio run -e esp32dev -t upload
pio device monitor
```

Default AP address: http://192.168.4.1

Protected requests use Authorization: Bearer <HID_API_SECRET>. Do not expose the unauthenticated HTTP transport to the public internet.

## API

See docs/api.md for command, queue, configuration, and WebSocket routes.

## Logger privacy

Logs and telemetry contain ESP32-originated command events and device diagnostics only. The firmware does not capture physical keyboard input from the connected computer. Typed text is not included in log messages.

## Feature branches

- main: core BLE HID, local control, queue, logging, and telemetry.
- feature/ota: OTA updater work is isolated from main and is not automatically merged.

## Validation status

A GitHub Actions PlatformIO build workflow is configured. Hardware pairing, BLE reconnection, Wi-Fi coexistence, and telemetry delivery still require validation on a physical ESP32-WROOM-32. Do not treat a successful compile as hardware certification.
