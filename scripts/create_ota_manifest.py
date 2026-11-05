#!/usr/bin/env python3
"""Create an ECDSA-signed ESP32-HID OTA manifest.

The private key stays on the release machine. Only the matching public key is
embedded in device firmware.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import subprocess
from pathlib import Path

BOARD = "esp32-wroom-32"
CHANNEL = "stable"
VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
HEX_RE = re.compile(r"^[0-9a-f]{64}$")


def canonical_payload(manifest: dict) -> bytes:
    fields = (
        ("schema", "1"),
        ("board", manifest["board"]),
        ("version", manifest["version"]),
        ("security_version", str(manifest["security_version"])),
        ("channel", manifest["channel"]),
        ("image_url", manifest["image_url"]),
        ("image_size", str(manifest["image_size"])),
        ("sha256", manifest["sha256"]),
    )
    return ("".join(f"{key}={value}\n" for key, value in fields)).encode("ascii")


def create_manifest(
    image: Path,
    version: str,
    security_version: int,
    image_url: str,
    private_key: Path,
    channel: str = CHANNEL,
) -> dict:
    if not VERSION_RE.fullmatch(version):
        raise ValueError("version must use numeric MAJOR.MINOR.PATCH form")
    if security_version < 1:
        raise ValueError("security-version must be >= 1")
    if channel != CHANNEL:
        raise ValueError("only the stable channel is currently supported")
    if not image_url.startswith("https://") or "@" in image_url or any(c.isspace() for c in image_url):
        raise ValueError("image URL must be an HTTPS URL without user-info or whitespace")
    if not image.is_file():
        raise ValueError(f"firmware image does not exist: {image}")
    if image.stat().st_size < 1024:
        raise ValueError("firmware image is unexpectedly small")

    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    manifest = {
        "schema": 1,
        "board": BOARD,
        "version": version,
        "security_version": security_version,
        "channel": channel,
        "image_url": image_url,
        "image_size": image.stat().st_size,
        "sha256": digest,
    }
    completed = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", str(private_key)],
        input=canonical_payload(manifest),
        capture_output=True,
        check=True,
    )
    manifest["signature"] = base64.b64encode(completed.stdout).decode("ascii")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--security-version", type=int, required=True)
    parser.add_argument("--image-url", required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    manifest = create_manifest(
        args.image,
        args.version,
        args.security_version,
        args.image_url,
        args.private_key,
    )
    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote signed OTA manifest: {args.output}")


if __name__ == "__main__":
    main()
