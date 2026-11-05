# OTA design — feature/ota

This document defines the OTA feature scope. It is a design specification, not an implemented updater.

## Goals

- Authenticated firmware update over the trusted network.
- Verify a cryptographic image signature before installation.
- Use HTTPS with certificate validation.
- Use ESP32 OTA partition slots and rollback.
- Keep the active image bootable if an update fails.

## Update lifecycle

1. Client requests device status and current firmware version.
2. Client submits update metadata (version, image size, SHA-256 digest, signature, release channel).
3. Device authenticates the request and checks policy (version, size, available partition, idle queue).
4. Device downloads over validated HTTPS into the inactive OTA slot.
5. Device verifies image signature and digest.
6. Device sets the new slot as boot target and reboots.
7. New firmware runs startup health checks and marks itself valid.
8. If boot or health checks fail, ESP-IDF rollback returns to the previous valid image.

## Security requirements

- Prefer asymmetric signature verification (public key embedded in firmware; private signing key kept offline).
- A shared API secret authorizes requests but is not proof that firmware is trusted.
- Reject unsigned, modified, oversized, incompatible, or disallowed downgrade images.
- Do not disable TLS certificate validation.
- Do not accept arbitrary filesystem URLs from unauthenticated clients.
- Rate-limit update attempts and log update lifecycle events without exposing credentials.

## Safety requirements

- OTA requires the command queue to be idle.
- Release all held keys before installation.
- Do not start an update while BLE pairing or a command sequence is in progress.
- Provide explicit progress, cancellation policy, and failure diagnostics.

## Hardware/software tasks

- Configure an OTA-capable partition table with two application slots.
- Enable bootloader rollback support.
- Implement signature verification and update metadata parser.
- Implement authenticated HTTPS download and write to inactive slot.
- Add update status endpoints and CLI status command.
- Test interrupted downloads, power loss, bad signatures, wrong chip/board, low space, and failed first boot.

## Status

Not implemented. This feature branch contains the plan only.
