# Agent continuation note

## Mission
Continue ESP32-HID as a pnpm/Turborepo monorepo for an original ESP32-WROOM-32. The device is one composite BLE keyboard + mouse. Web, CLI and MCP must use one shared device API.

## Layout
- firmware/esp32 — PlatformIO firmware and composite BLE HID.
- apps/web — Next.js monochrome dashboard.
- apps/cli — Commander CLI.
- apps/mcp — MCP SDK v2 server, HTTP and stdio.
- packages/device-client — shared typed API client.
- docs — API, architecture, security, recording, roadmap and OTA notes.

## Start
1. Copy .env.example to .env and set ESP32_API_SECRET, ESP32_AP_PASSWORD and MCP_AUTH_TOKEN.
2. pnpm install.
3. pnpm dev (web + MCP HTTP through Turborepo).
4. pnpm cli -- status.
5. pnpm mcp:stdio for local MCP clients.
6. Install pioarduino Core 6.2.0+ and run pnpm firmware:build.

## Non-negotiable
- Root .env is the single shared configuration source.
- Only firmware uses BLE HID. All other surfaces call the same API.
- Call ble.update() frequently; do not block BLE callbacks or queue processing.
- Queue is bounded, sequential and non-blocking.
- PAUSE preserves queue; RESUME continues; STOP clears and releases all controls.
- Record explicit ESP32-originated commands only, never host physical keyboard input.
- Telemetry is HTTPS-only, bounded and excludes typed text and secrets.
- No stealth or security-evasion.
- Future work uses separate branches and PRs; no destructive rewrites or auto-merge.
- Test physical WROOM-32 and document host pairing behaviour using docs/hardware-validation.md.
- Run `pnpm hardware:ble` on a paired Linux host to observe real keyboard/mouse input events.
- Run `pnpm hardware:smoke record hw-smoke`, power-cycle, then `pnpm hardware:smoke verify hw-smoke`.
- Validate signed HTTPS OTA, hash/signature rejection, interrupted-power safety and rollback on real board; see docs/ota.md.

## Validation checklist
- pnpm install, pnpm typecheck, web build.
- pioarduino Core 6.2.0+ build for esp32dev.
- Test composite keyboard/mouse on real host; remove and re-pair after descriptor changes.
- Exercise reconnect advertising, Wi-Fi coexistence, queue pause/resume/stop and macro NVS limits.
- Verify WebSocket auth and telemetry CA/retry.
- Verify MCP HTTP guards and stdio in a real MCP host.
- Review legacy OTA: image signature verification, anti-rollback and boot health rollback are mandatory.

## Latest CI baseline
- As of 2026-11-05, PR #1 CI passed: workspace typecheck, Next.js production build and PlatformIO firmware build.
- This is compile-time validation only. Do not claim hardware validation until a physical ESP32-WROOM-32 is tested against at least one desktop host.
- pnpm-lock.yaml is committed and CI uses `pnpm install --frozen-lockfile`.
- `pnpm test` covers the shared device-client API contracts.
