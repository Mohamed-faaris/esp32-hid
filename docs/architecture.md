# Architecture

## Runtime components

- **BLE HID task:** owns the BLE HID profile and host connection state.
- **Wi-Fi/API task:** AP+STA lifecycle, HTTP routes, authentication, WebSocket clients.
- **Command dispatcher:** validates requests and places normalized commands into one bounded queue.
- **Queue executor:** executes commands sequentially; API and Serial CLI share this path.
- **Logger:** bounded ring buffer plus serial and WebSocket sinks.
- **Telemetry worker:** optional external HTTP delivery; never blocks HID execution.
- **Config storage:** versioned NVS settings; secrets must not be exposed by read endpoints.

Use FreeRTOS queues and tasks with bounded buffers. Do not block BLE callbacks or HTTP handlers on delays, network requests, or long command sequences.

## Queue semantics

- `PAUSE`: finish or safely release the current key operation, then suspend; preserve pending commands.
- `RESUME`: continue from the next pending command.
- `STOP`: cancel current sequence, release all held keys, clear pending queue.
- Every command has an ID and terminal status (completed, failed, cancelled).

## Bluetooth host management

Persist bonded host metadata/preferences, not assumptions about multiple simultaneous HID hosts. Initially support one active host, with explicit scan/pair/connect/disconnect/reconnect/forget operations. Reconnection must be user-triggered or an explicit opt-in auto-reconnect setting.

## Operating modes

- `NORMAL`, `TYPING`, `INSTANT`, `SEQUENCE`, `RANDOMIZED`, `SILENT`, `DIAGNOSTIC`
- Modes configure execution/logging behaviour; they do not implement stealth or detection avoidance.
- Random timing variation is bounded and intended for repeatable automation tests.

## Logging and telemetry

Logger sources are ESP32-generated command events, HID key events emitted by this device, connectivity, queue transitions, and diagnostics. Do not implement host-side keyboard capture.

Levels: `NONE`, `ERROR`, `WARN`, `INFO`, `DEBUG`, `TRACE`.

Outputs: serial, bounded in-memory ring buffer, authenticated WebSocket stream, optional external HTTPS endpoint. Remote delivery uses a separate task, bounded retry/backoff, and a capped offline queue. Secrets and typed text are excluded by default.

## OTA (separate feature branch)

OTA work is isolated on `feature/ota`; it is not part of the initial baseline. Design requirements:

- Authenticated update initiation and strict image size/type validation.
- Firmware image signature verification (do not rely on a shared password alone).
- HTTPS update transport with certificate validation; no insecure fallback.
- Partition table with OTA slots and rollback support.
- Verify boot and mark image valid only after startup health checks.
- Reject downgrade/rollback to older vulnerable firmware unless explicitly authorized.
- Progress, failure reason, current version, and pending verification exposed in status/logs.
- Never flash while a keyboard sequence is executing; require idle state and release all held keys.
