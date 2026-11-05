#pragma once
// Manual build template. Normal PlatformIO builds generate secrets.h from root .env.
#define HID_API_SECRET "replace-with-a-long-random-secret"
#define HID_AP_SSID "ESP32-HID"
#define HID_AP_PASSWORD "replace-with-a-strong-password"
#define HID_STA_SSID ""
#define HID_STA_PASSWORD ""
#define HID_TELEMETRY_ROOT_CA ""
#define HID_OTA_ROOT_CA ""
#define HID_OTA_PUBLIC_KEY ""
