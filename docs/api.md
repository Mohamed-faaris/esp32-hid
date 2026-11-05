# API contract (planned)

All protected endpoints require:

```http
Authorization: Bearer <device-secret>
Content-Type: application/json
```

## Keyboard and queue

- `GET /api/status`
- `POST /api/keyboard/type`
- `POST /api/keyboard/press`
- `POST /api/keyboard/hotkey`
- `POST /api/keyboard/sequence`
- `POST /api/queue/pause`
- `POST /api/queue/resume`
- `POST /api/queue/stop`
- `GET /api/queue`

## Bluetooth host management

- `GET /api/bluetooth/devices`
- `POST /api/bluetooth/scan`
- `POST /api/bluetooth/pair`
- `POST /api/bluetooth/connect`
- `POST /api/bluetooth/disconnect`
- `POST /api/bluetooth/reconnect`
- `POST /api/bluetooth/forget`

## Logging and telemetry

- `GET /api/logs`
- `GET /api/logs/stream` (WebSocket upgrade)
- `POST /api/logs/config`
- `POST /api/telemetry/config`

Logger events describe ESP32-originated commands and device diagnostics only. No host-side keyboard capture.

## OTA (feature/ota only)

The OTA branch will add authenticated update metadata/status and update initiation endpoints after signature verification and partition support are implemented. Exact route names and payloads will be documented with the firmware implementation; this scaffold does not expose a working OTA endpoint.
