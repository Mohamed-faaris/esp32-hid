import base64
import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

import create_ota_manifest as manifest_tool


class SignedManifestTests(unittest.TestCase):
    def test_manifest_signature_matches_canonical_payload(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            image = root / "firmware.bin"
            image.write_bytes(bytes(range(256)) * 16)
            private_key = root / "ota-private.pem"
            public_key = root / "ota-public.pem"
            subprocess.run(
                ["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(private_key)],
                check=True,
                capture_output=True,
            )
            subprocess.run(
                ["openssl", "ec", "-in", str(private_key), "-pubout", "-out", str(public_key)],
                check=True,
                capture_output=True,
            )

            manifest = manifest_tool.create_manifest(
                image=image,
                version="1.1.0",
                security_version=1,
                image_url="https://updates.example.test/firmware.bin",
                private_key=private_key,
            )
            self.assertEqual(manifest["sha256"], hashlib.sha256(image.read_bytes()).hexdigest())
            signature = base64.b64decode(manifest["signature"])
            signature_path = root / "manifest.sig"
            signature_path.write_bytes(signature)

            valid = subprocess.run(
                ["openssl", "dgst", "-sha256", "-verify", str(public_key), "-signature", str(signature_path)],
                input=manifest_tool.canonical_payload(manifest),
                capture_output=True,
            )
            self.assertEqual(valid.returncode, 0, valid.stderr.decode())

            tampered = dict(manifest, version="1.2.0")
            invalid = subprocess.run(
                ["openssl", "dgst", "-sha256", "-verify", str(public_key), "-signature", str(signature_path)],
                input=manifest_tool.canonical_payload(tampered),
                capture_output=True,
            )
            self.assertNotEqual(invalid.returncode, 0)

    def test_rejects_http_image_urls(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            image = root / "firmware.bin"
            image.write_bytes(b"x" * 2048)
            key = root / "key.pem"
            subprocess.run(
                ["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(key)],
                check=True,
                capture_output=True,
            )
            with self.assertRaises(ValueError):
                manifest_tool.create_manifest(
                    image, "1.1.0", 1, "http://updates.example.test/firmware.bin", key
                )


if __name__ == "__main__":
    unittest.main()
