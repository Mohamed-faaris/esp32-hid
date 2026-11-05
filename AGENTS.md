# Agent working agreement

## Project
ESP32-HID is a local-first control plane for an original ESP32-WROOM-32. It appears to a paired computer as a composite BLE keyboard and mouse. Web, CLI and MCP call the same firmware HTTP API.

## Layout
- firmware/esp32: PlatformIO firmware; only this package talks to the BLE HID library.
- apps/web: Next.js monochrome dashboard.
- apps/cli: terminal client.
- apps/mcp: MCP HTTP and stdio server.
- packages/device-client: shared typed REST client.
- docs: API, architecture, security, recording, roadmap and OTA notes.

## Commands
1. Copy .env.example to .env and set unique secrets.
2. Run pnpm install.
3. Run pnpm dev to start web and MCP HTTP concurrently through Turborepo.
4. Run pnpm cli -- status for CLI control.
5. Run pnpm mcp:stdio for a local MCP host.
6. Install pioarduino Core 6.2.0+ and run pnpm firmware:build.

## Rules
- Root .env is the one operator-managed configuration source. Never create per-app env copies or NEXT_PUBLIC secrets.
- Web, CLI and MCP use packages/device-client and the same REST contract.
- All HID operations enter the bounded firmware queue. Keep ble.update() responsive.
- PAUSE preserves pending work; RESUME continues; STOP clears queued work and releases keyboard/mouse state.
- Recording stores commands explicitly sent through this controller. Never capture host physical keyboard input.
- Logs and telemetry never include API secrets, Wi-Fi credentials or raw typed text.
- Mouse deltas are relative and bounded.
- Keep generated secrets.h ignored.
- Do not implement stealth or security-detection evasion.
- Add future capabilities on separate branches and PRs. Never force-push or merge without explicit direction.
- Preserve NVS compatibility with versioned migrations.
- Label hardware verification honestly.

## Known constraints
- BLE pairing and reconnection are host-OS controlled.
- One active HID host is the initial supported configuration.
- OTA remains isolated and is not release-ready until image signatures, anti-rollback and first-boot health rollback are tested.
