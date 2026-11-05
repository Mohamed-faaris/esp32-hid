# ESP32 HID Control Center

A local-first control plane for an ESP32-WROOM-32 that appears to a paired computer as one BLE composite keyboard + mouse. Control it from a monochrome web dashboard, terminal CLI, MCP server for AI agents, or firmware Serial CLI.

This is a pnpm/Turborepo monorepo. Every control surface calls the same firmware HTTP API.

## Features

- Composite BLE HID keyboard and mouse on the original ESP32.
- Wi-Fi Access Point + optional Station mode.
- Authenticated HTTP API and authenticated WebSocket log stream.
- Shared sequential command queue: pause, resume, stop-and-release.
- Keyboard text, key taps, hotkeys, configurable speed, sleep and bounded timing jitter.
- Mouse relative move, click and wheel.
- Explicit macro recording/playback saved to device NVS.
- Serial CLI, Node CLI, Next.js dashboard and MCP tools.
- Configurable log levels, bounded event ring buffer, optional HTTPS telemetry.
- Root .env is the single operator-managed configuration source.
- Future features and OTA remain isolated on feature branches and PRs.

## Architecture

    Next.js Web ─┐
    Node CLI ────┼── shared device-client ── ESP32 HTTP API ── command queue
    MCP server ──┘                                             │
                                                     EspBle composite HID
                                                        /          \
                                                    Keyboard      Mouse

## Requirements

- Node.js 22+
- pnpm 10+
- pioarduino PlatformIO Core 6.2.0+ for the current EspBle / Arduino-ESP32 3.x stack
- ESP32-WROOM-32 / ESP32 DevKit V1
- BLE HID capable host computer or phone

The current EspBle release documents Arduino-ESP32 Core 3.2.0 as the minimum for the original ESP32, with tested core versions through 3.3.11. Standard PlatformIO's stable Espressif platform still uses the older 2.x Arduino core, so firmware/esp32/platformio.ini uses the pioarduino platform instead. See the EspBle compatibility documentation and PlatformIO framework guidance.

## Quick start

    git clone https://github.com/Mohamed-faaris/esp32-hid.git
    cd esp32-hid
    cp .env.example .env
    # Set ESP32_API_SECRET, ESP32_AP_PASSWORD and MCP_AUTH_TOKEN.
    pnpm install
    pnpm test
    pnpm dev

Turborepo starts the web dashboard and MCP HTTP service in parallel:
- Web: http://localhost:3000 (WEB_PORT configurable)
- MCP: http://127.0.0.1:3333/mcp (MCP_HOST/MCP_PORT configurable)

## Firmware build and flash

    pnpm firmware:build
    pnpm firmware:upload
    pnpm --filter @esp32-hid/firmware monitor

The firmware build script generates ignored firmware/esp32/include/secrets.h from the root .env. Never commit that generated file. The AP defaults to ESP32-HID at 192.168.4.1. Configure ESP32_API_URL for the device address reachable from your laptop.

## Control surfaces

### Web dashboard
Monochrome shadcn-inspired dashboard with keyboard, mouse, queue, settings, macro recording and activity panels.

### Node CLI

    pnpm cli -- status
    pnpm cli -- type "Hello from ESP32"
    pnpm cli -- hotkey CTRL+SHIFT+ESC
    pnpm cli -- mouse move 15 -8
    pnpm cli -- mouse click left
    pnpm cli -- queue pause
    pnpm cli -- queue resume
    pnpm cli -- queue stop
    pnpm cli -- record start demo
    pnpm cli -- record stop
    pnpm cli -- macro list
    pnpm cli -- macro play demo
    pnpm cli -- bluetooth-advertise

### MCP

- pnpm dev starts the authenticated local Streamable HTTP MCP endpoint.
- pnpm mcp:stdio starts the stdio transport for local agent hosts.
- Tools expose status, keyboard, mouse, queue control, modes, logs, advertising restart and macros.

The HTTP MCP service binds to 127.0.0.1 by default and requires a separate MCP_AUTH_TOKEN.

### Firmware Serial CLI
Open the serial monitor at 115200 baud and type help. Supports status, type, key, hotkey, mouse move/click/wheel, pause/resume/stop, record start/stop and macro list/play.

## Security and privacy

- Protected firmware routes use Authorization: Bearer ESP32_API_SECRET.
- Firmware HTTP is unencrypted; keep the device on a trusted LAN and do not port-forward it.
- MCP HTTP binds to loopback, validates Host/Origin and requires MCP_AUTH_TOKEN.
- Telemetry requires HTTPS and a configured CA certificate.
- Logs and telemetry exclude typed text and credentials.
- Macro recording stores only commands intentionally sent to this controller; it does not capture physical keyboard input on the connected host.
- No stealth or detection-evasion behaviour is implemented.

## Bluetooth limitation

The ESP32 is a BLE HID peripheral. Pairing is initiated from the host OS. After disconnection, firmware can restart advertising, but the computer decides whether to reconnect. Firmware cannot force host-side pairing or inspect the computer's saved Bluetooth devices. One active host is the initial target.

## Documentation

See docs/README.md, docs/api.md, docs/architecture.md, docs/security.md, docs/recording.md, docs/ROADMAP.md and AGENT_HANDOFF.md.

## Status

The implementation is on feature/monorepo-control-plane as draft PR #1. GitHub Actions currently passes the workspace typecheck, web production build and ESP32 firmware build. Physical ESP32-WROOM-32 validation is still required for BLE pairing, keyboard/mouse reports, reconnect, Wi-Fi coexistence, queue timing and macro persistence. OTA is not implemented in this branch and is not release-ready.
