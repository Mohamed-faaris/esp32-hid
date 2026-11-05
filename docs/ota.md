# OTA status

OTA is intentionally excluded from the core monorepo baseline.

The legacy feature/ota branch contains an experimental HTTPS updater with SHA-256 checking. It is not release-ready: a digest sent with the same authenticated request is not an independent firmware signature. It also needs anti-rollback, secure-boot integration and first-boot health confirmation/rollback tests.

Do not merge the legacy OTA branch as-is.

A future OTA PR must:
1. Verify an image signature using an embedded public key.
2. Use HTTPS with certificate validation and no insecure fallback.
3. Validate board, image size, version, partition and release channel.
4. Enforce downgrade/anti-rollback policy.
5. Install to the inactive OTA partition.
6. Require an idle queue and release all HID controls.
7. Confirm health on first boot and roll back on failed checks.
8. Test interrupted download, bad signature, wrong image, power loss and failed boot on a real WROOM-32.
