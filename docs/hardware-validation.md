# ESP32-WROOM-32 hardware validation

Software builds and API tests do not prove BLE HID behavior. Run this procedure with an original ESP32-WROOM-32 / ESP32 DevKit V1 connected over USB and a desktop Bluetooth host.

## Preconditions

1. Flash the firmware produced by `pnpm firmware:build`.
2. Connect the host computer to the ESP32 Wi-Fi AP (default SSID `ESP32-HID`) or put both on the same trusted LAN.
3. Pair `ESP32-HID` from the host OS Bluetooth settings. On Linux, use `bluetoothctl`:
   ```text
   power on
   agent on
   default-agent
   scan on
   pair <ESP32-HID-MAC>
   trust <ESP32-HID-MAC>
   connect <ESP32-HID-MAC>
   ```
4. Set `ESP32_API_URL` and `ESP32_API_SECRET` in the root `.env`, then run `pnpm install`.

The BLE keyboard and mouse readiness flags must both be true in `GET /api/status`. Pairing is host initiated; the device cannot force a host reconnect.

## Automated device-side checks

Run:

```sh
pnpm hardware:smoke record hw-smoke
```

This checks that both BLE interfaces report ready, that pause preserves queued commands, resume drains them, stop clears the queue, and an explicitly-created keyboard/mouse macro is saved and indexed in NVS.

Next, power-cycle the ESP32 (remove USB power for several seconds), reconnect the host, then run:

```sh
pnpm hardware:smoke verify hw-smoke
```

This requires the macro to still be listed after reboot and to replay successfully. The test sends visible text beginning with `ESP32-HID-` and moves the mouse by a small relative delta; do not run while a sensitive text field or destructive UI control is focused.

## Manual host-side checks

Record the results in your release checklist:

- [ ] BLE HID device appears as a keyboard and mouse.
- [ ] Verify the automated macro text (`ESP32-HID-hw-smoke`) and `QUEUE-PAUSE-RESUME` appear in a harmless text editor. Also type `ESP32-HID-MANUAL-TEST` manually and verify exact output.
- [ ] Verify mouse relative movement and wheel movement in a harmless empty area.
- [ ] Disconnect from host Bluetooth; verify advertising resumes; reconnect from host settings.
- [ ] Repeat keyboard and mouse checks after reconnect.
- [ ] Repeat while Wi-Fi AP is active and while STA is connected to a trusted network.
- [ ] Run the two-phase macro test above after a full power cycle.
- [ ] Verify STOP releases a held keyboard or mouse state and no later queued commands execute.

Do not mark these as passed until observed on the actual host. Save host OS/version, board revision, firmware commit and test date with the results.
