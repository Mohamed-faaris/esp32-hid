# ESP32 HID Controller

BLE HID keyboard controller for the **original ESP32 / ESP32-WROOM-32**, controlled through Wi-Fi (AP/STA), HTTP API, and Serial CLI.

> This repository starts with an architecture scaffold. Hardware-specific BLE behaviour and the full firmware are implemented and validated in later milestones.

## Target hardware

- ESP32-WROOM-32 (Bluetooth Classic + BLE; no native USB device HID)
- BLE HID keyboard profile
- Wi-Fi AP, STA, and AP+STA modes

The initial release targets **one active bonded host** at a time, with saved bonds and explicit scan/pair/connect/disconnect/reconnect/forget controls. OTA is developed separately on `feature/ota`.

## Planned capabilities

- BLE keyboard: text, key presses, modifiers, hotkeys, release-all
- Host manager: scan, pair/bond, connect, disconnect, reconnect, forget, optional auto-reconnect
- Network: AP + STA, configurable credentials, HTTP API and WebSocket log stream
- Shared-secret authentication for protected API operations
- One sequential command queue shared by API and Serial CLI
- Queue controls: `PAUSE` preserves pending work, `RESUME` continues, `STOP` releases keys and clears pending work
- Typing modes: instant, character-by-character, configurable speed, bounded timing jitter, sleep/delay, sequences
- Persistent configuration in NVS
- Structured logs, in-memory ring buffer, WebSocket streaming, optional external HTTP telemetry with retry/backoff and bounded offline queue

## Safety and privacy boundaries

- Logger modules record **ESP32-originated commands and HID events only** (plus diagnostics).
- No capture of unrelated physical keyboard input on a connected computer.
- No stealth, security-detection evasion, or covert data collection.
- Remote telemetry is disabled by default. Configure an HTTPS endpoint and credentials explicitly.
- Never log API secrets, API keys, or typed text by default. Text logging must be an explicit opt-in for controlled test input.
- Do not expose the HTTP API directly to the public internet. The initial shared-secret scheme does not encrypt HTTP traffic; use trusted networks and HTTPS-capable gateway infrastructure for remote access.

## Build

This scaffold uses PlatformIO with the Arduino framework. Install PlatformIO Core, then:

```sh
pio run
pio run -t upload
pio device monitor
```

See [docs/architecture.md](docs/architecture.md) and [docs/api.md](docs/api.md).

## Status

Architecture scaffold only; firmware modules are not yet implemented. No claim of hardware validation is made.
