# HTTP API

Base URL: http://192.168.4.1 in AP mode or the DHCP address in STA mode.

Protected routes require Authorization: Bearer <HID_API_SECRET>. Status and Bluetooth status are read-only and public.

## Keyboard commands

- POST /api/keyboard/type: {"type":"type","value":"Hello","delay_ms":35,"jitter_ms":0,"instant":false}
- POST /api/keyboard/press: {"type":"press","value":"ENTER"}
- POST /api/keyboard/hotkey: {"type":"hotkey","value":"CTRL+SHIFT+ESC"}
- POST /api/keyboard/sleep: {"type":"sleep","ms":1000}
- POST /api/keyboard/sequence: {"commands":[{"type":"type","value":"hello"},{"type":"sleep","ms":500},{"type":"press","value":"ENTER"}]}

## Queue and configuration

- POST /api/queue/pause, /api/queue/resume, /api/queue/stop
- GET /api/status
- POST /api/keyboard/config: {"delay_ms":40,"jitter_ms":10}
- POST /api/logs/config: raw body INFO, DEBUG, TRACE, WARN, ERROR, or NONE
- WebSocket logs: ws://<device-ip>:81/
- POST /api/telemetry/config stores endpoint credentials, but outbound delivery is not implemented in this firmware revision and returns HTTP 501.

## Bluetooth

The ESP32 is a BLE HID peripheral. Pairing and reconnect are initiated from the computer Bluetooth settings. The ESP32 cannot force a remote host to reconnect or enumerate the host's saved bond list. GET /api/bluetooth/status reports connection state.

## Serial CLI

help, status, pause, resume, stop, type <text>, press <key>, hotkey <A+B>.

## Security

The initial shared bearer secret travels over local HTTP without encryption. Use only on a trusted isolated LAN; do not port-forward. No host-side physical keyboard capture is implemented.
