#!/usr/bin/env python3
"""Verify real BLE HID input reports on a paired Linux host.

Requires Linux evdev access to /dev/input/event* and a connected ESP32-HID
keyboard/mouse. This sends the short text "HID" and moves the pointer slightly.
"""
from __future__ import annotations

import glob
import json
import os
import selectors
import struct
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EVENT = struct.Struct("@llHHi")
EV_KEY = 0x01
EV_REL = 0x02
REL_X = 0x00
REL_Y = 0x01
KEY_H = 35
KEY_I = 23
KEY_D = 32
DEVICE_NAME = "ESP32-HID"


def read_env() -> dict[str, str]:
    values: dict[str, str] = {}
    path = ROOT / ".env"
    if not path.exists():
        return values
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\\"":
            value = value[1:-1]
        values[key.strip()] = value.replace("\\n", "\n")
    return values


def input_device_name(fd: int) -> str:
    request = 0x80000000 | (256 << 16) | (ord("E") << 8) | 0x06
    raw = bytearray(256)
    try:
        import fcntl
        fcntl.ioctl(fd, request, raw, True)
        return raw.split(b"\\0", 1)[0].decode("utf-8", errors="replace")
    except OSError:
        return ""


def api(base: str, token: str, path: str, body: dict | None = None) -> dict:
    data = None if body is None else json.dumps(body).encode("utf-8")
    headers = {"Authorization": "Bearer " + token}
    if data is not None:
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(base.rstrip("/") + "/api/" + path, data=data, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=8) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"/api/{path} returned HTTP {error.code}: {detail}") from error


def main() -> int:
    env = read_env()
    base = os.environ.get("ESP32_API_URL", env.get("ESP32_API_URL", "http://192.168.4.1"))
    token = os.environ.get("ESP32_API_SECRET", env.get("ESP32_API_SECRET", ""))
    if not token:
        print("FAIL: ESP32_API_SECRET is missing from environment or root .env.", file=sys.stderr)
        return 2

    status = api(base, token, "status")
    if not status.get("ble_keyboard_ready") or not status.get("ble_mouse_ready"):
        print("FAIL: pair/connect ESP32-HID first; both BLE interfaces must be ready.", file=sys.stderr)
        return 2

    handles: list[int] = []
    names: list[str] = []
    selector = selectors.DefaultSelector()
    try:
        for path in sorted(glob.glob("/dev/input/event*")):
            try:
                fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
            except PermissionError:
                raise RuntimeError(
                    f"No permission to read {path}. Add your user to the input group, log out/in, "
                    "or run this diagnostic with sudo."
                )
            name = input_device_name(fd)
            if DEVICE_NAME.casefold() not in name.casefold():
                os.close(fd)
                continue
            handles.append(fd)
            names.append(f"{path}: {name}")
            selector.register(fd, selectors.EVENT_READ)

        if not handles:
            raise RuntimeError(
                "No ESP32-HID Linux input devices found. Pair/connect the device first and "
                "confirm the desktop created /dev/input/event* nodes."
            )

        print("Monitoring actual Linux input events:")
        for name in names:
            print("  " + name)

        # Queue input after the event nodes are open so key and relative-motion reports
        # remain available to this process even if the host receives them immediately.
        api(base, token, "keyboard/type", {"type": "type", "value": "HID", "instant": True})
        api(base, token, "mouse/move", {"dx": 23, "dy": -11, "wheel": 0})

        key_codes: set[int] = set()
        rel_x = False
        rel_y = False
        deadline = time.monotonic() + 8.0
        while time.monotonic() < deadline and not ({KEY_H, KEY_I, KEY_D} <= key_codes and rel_x and rel_y):
            for key, _ in selector.select(timeout=0.25):
                try:
                    packet = os.read(key.fd, EVENT.size * 64)
                except BlockingIOError:
                    continue
                usable = len(packet) - (len(packet) % EVENT.size)
                for offset in range(0, usable, EVENT.size):
                    _sec, _usec, event_type, code, value = EVENT.unpack_from(packet, offset)
                    if event_type == EV_KEY and value == 1:
                        key_codes.add(code)
                    elif event_type == EV_REL and value:
                        if code == REL_X:
                            rel_x = True
                        elif code == REL_Y:
                            rel_y = True

        missing = []
        for code, label in ((KEY_H, "H"), (KEY_I, "I"), (KEY_D, "D")):
            if code not in key_codes:
                missing.append("KEY_" + label)
        if not rel_x:
            missing.append("REL_X")
        if not rel_y:
            missing.append("REL_Y")
        if missing:
            raise RuntimeError(
                "Timed out waiting for actual BLE input events: " + ", ".join(missing)
            )

        print("PASS: host received keyboard key-down reports for H, I and D.")
        print("PASS: host received non-zero relative mouse X and Y reports.")
        print("Note: text HID was typed into the currently focused application.")
        return 0
    finally:
        selector.close()
        for fd in handles:
            os.close(fd)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
