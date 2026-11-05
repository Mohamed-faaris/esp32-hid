#pragma once

// Copy to include/secrets.h and replace the placeholders.
// secrets.h is gitignored. Never commit real credentials.
#define HID_API_SECRET "replace-with-a-long-random-secret"
#define HID_AP_SSID "ESP32-HID"
#define HID_AP_PASSWORD "replace-with-a-strong-password"
#define HID_STA_SSID ""
#define HID_STA_PASSWORD ""

// Paste the PEM root CA certificate for your telemetry HTTPS server here.
// Remote telemetry stays disabled when this is empty; never use insecure TLS.
#define HID_TELEMETRY_ROOT_CA ""
