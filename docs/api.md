# ESP32 device API

Base URL defaults to http://192.168.4.1 in AP mode.

Protected routes require:

    Authorization: Bearer <ESP32_API_SECRET>
    Content-Type: application/json

## Status and queue

- GET /api/status
- GET /api/queue
- POST /api/queue/pause
- POST /api/queue/resume
- POST /api/queue/stop — clears pending commands and releases keyboard and mouse.

## Keyboard

- POST /api/keyboard/type — {"type":"type","value":"Hello","delay_ms":35,"jitter_ms":0,"instant":false}
- POST /api/keyboard/press — {"type":"press","value":"ENTER"}
- POST /api/keyboard/hotkey — {"type":"hotkey","value":"CTRL+SHIFT+ESC"}
- POST /api/keyboard/sleep — {"type":"sleep","ms":1000}
- POST /api/keyboard/sequence — {"commands":[{"type":"type","value":"hello"},{"type":"sleep","ms":500},{"type":"press","value":"ENTER"}]}
- POST /api/keyboard/config — {"delay_ms":40,"jitter_ms":10}

## Mouse

All coordinates are relative. Firmware clamps each x/y delta to ±100 counts.

- POST /api/mouse/move — {"dx":20,"dy":-10,"wheel":0}
- POST /api/mouse/click — {"button":"left"} (left/right/middle/back/forward)
- POST /api/mouse/wheel — {"delta":5}

## Modes, logging and network

- POST /api/mode — {"mode":"NORMAL"}. Modes: NORMAL, TYPING, INSTANT, SEQUENCE, RANDOMIZED, QUIET, DIAGNOSTIC.
- GET /api/logs — bounded log ring buffer, requires authentication.
- POST /api/logs/config — {"level":"INFO"}. Levels: NONE, ERROR, WARN, INFO, DEBUG, TRACE.
- WebSocket: ws://<device-ip>:81/. Send the device secret as the first message; wait for AUTH_OK before logs.
- POST /api/wifi/config — {"ssid":"network","password":"password"}.
- POST /api/telemetry/config — {"enabled":true,"endpoint":"https://example.net/events","api_key":"..."}.

## Macro recording

- POST /api/recording/start — {"name":"demo"}
- POST /api/recording/stop
- GET /api/recording/status
- GET /api/macros
- POST /api/macros/play — {"name":"demo"}
- POST /api/macros/delete — {"name":"demo"}

Recording stores commands deliberately submitted through this ESP32 controller only. It does not capture physical keyboard input on the connected computer.

## Bluetooth

- GET /api/bluetooth/status
- POST /api/bluetooth/reconnect — restarts peripheral advertising. The host OS remains responsible for reconnecting.

## Errors

- 400: malformed JSON, invalid command or out-of-range input.
- 401: missing or invalid bearer secret.
- 404: unknown route or macro.
- 409: recording state conflict.
- 429: bounded command queue full.
- 502: control-plane proxy cannot reach the device.
