# Architecture

    Root .env
       |
    pnpm + Turborepo
      |       |       |
     Web     CLI     MCP
       \      |      /
        Shared device-client
                |
        ESP32 HTTP API :80
                |
     Queue / Logger / Telemetry
                |
       EspBle composite BLE HID
            /          \
       Keyboard       Mouse

## Firmware
The original ESP32-WROOM-32 runs one composite BLE HID peripheral. EspBle composes keyboard and mouse reports into one HID service. The main loop calls ble.update(), services HTTP/WebSocket/Serial and advances a bounded non-blocking queue.

## Control planes
- Web: Next.js monochrome shadcn-inspired dashboard and same-origin device API proxy.
- CLI: Commander terminal client.
- MCP: SDK v2 Streamable HTTP on loopback and stdio transport for local agent hosts.
- Device client: shared typed REST wrapper.

## Common configuration
Root .env is the one operator-managed config source. pnpm dev loads it before Turborepo starts web and MCP in parallel. CLI and MCP load the root file when launched directly. Firmware build generates ignored secrets.h.

## Command queue
All interfaces call the same device API. PAUSE preserves pending commands; RESUME continues; STOP clears pending work and releases all HID state.

## Recording and privacy
Macro recording stores commands intentionally submitted via this controller. It does not capture unrelated physical keyboard input on the connected computer.

## Bluetooth constraint
The ESP32 is a BLE HID peripheral. Pairing and host-side reconnect are controlled by the OS. The ESP32 can restart advertising after disconnection but cannot force the computer to reconnect. One active host is the initial target.
