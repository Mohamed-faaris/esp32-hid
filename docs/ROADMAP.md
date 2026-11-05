# Roadmap — separate feature branches and PRs

Future work should be developed as independent, reviewable branches and PRs. Do not destabilize main or silently change NVS formats.

## P0 — reliability and safety
- API contract tests against a fake device.
- HMAC request signing, timestamp/nonce replay defense and credential rotation.
- Durable telemetry spool on LittleFS/SD with acknowledgements and deduplication.
- Versioned NVS migrations and factory reset.
- Watchdog, heap, queue saturation and reconnect diagnostics.
- OTA hardening: signed images, anti-rollback and first-boot health rollback.

## P1 — HID expansion
- Consumer-control media keys.
- Optional gamepad report.
- Pointer drag/hold/release and acceleration profiles.
- Macro import/export JSON, checksums and duration caps.
- Macro editor with step reorder, validation and dry run.

## P2 — operations and integrations
- Multiple bonded-host profiles and active host selection (subject to ESP32 stack limits).
- mDNS discovery and multi-device dashboard.
- MQTT telemetry with explicit opt-in and topic ACLs.
- Health metrics: heap, uptime, Wi-Fi RSSI, BLE reconnects, queue drops.
- Firmware versioning and release artifacts.

## P3 — productization
- Separate ESP32-S3 USB HID firmware target.
- Signed CLI release packages and MCP onboarding snippets.
- Accessibility, keyboard navigation and localization.
- Audit export and retention controls.

## PR policy
One feature per branch/PR. Add tests before changing API contracts. Preserve existing NVS via migrations. State what has and has not been hardware-tested.
