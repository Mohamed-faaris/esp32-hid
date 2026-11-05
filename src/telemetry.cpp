#include "telemetry.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef HID_TELEMETRY_ROOT_CA
#define HID_TELEMETRY_ROOT_CA ""
#endif

struct TelemetryEvent {
  char eventName[32];
  char detail[96];
  uint32_t uptime;
};
static QueueHandle_t telemetryQueue = nullptr;
static Preferences telemetryPrefs;
static String telemetryUrl;
static String telemetryKey;
static bool telemetryOn = false;

bool telemetryConfigure(const String& endpoint, const String& apiKey, bool enabled) {
  if (endpoint.length() && !endpoint.startsWith("https://")) return false;
  telemetryUrl = endpoint;
  telemetryKey = apiKey;
  telemetryOn = enabled && endpoint.length() && apiKey.length();
  telemetryPrefs.putString("url", telemetryUrl);
  telemetryPrefs.putString("key", telemetryKey);
  telemetryPrefs.putBool("enabled", telemetryOn);
  return true;
}

void telemetryPublish(const char* eventName, const char* detail) {
  if (!telemetryOn || !telemetryQueue) return;
  TelemetryEvent event{};
  strlcpy(event.eventName, eventName, sizeof(event.eventName));
  strlcpy(event.detail, detail, sizeof(event.detail));
  event.uptime = millis();
  // Bounded queue: drop newest when full; HID execution must never block.
  xQueueSend(telemetryQueue, &event, 0);
}

static void telemetryTask(void*) {
  TelemetryEvent event;
  for (;;) {
    if (xQueueReceive(telemetryQueue, &event, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
    if (!telemetryOn || WiFi.status() != WL_CONNECTED) continue;
    if (strlen(HID_TELEMETRY_ROOT_CA) < 100) continue; // No insecure TLS fallback.
    JsonDocument body;
    body["device"] = "ESP32-HID";
    body["event"] = event.eventName;
    body["detail"] = event.detail;
    body["uptime_ms"] = event.uptime;
    String payload;
    serializeJson(body, payload);

    bool delivered = false;
    for (uint8_t attempt = 0; attempt < 3 && !delivered; ++attempt) {
      WiFiClientSecure client;
      client.setCACert(HID_TELEMETRY_ROOT_CA);
      HTTPClient http;
      if (http.begin(client, telemetryUrl)) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", String("Bearer ") + telemetryKey);
        int code = http.POST(payload);
        delivered = code >= 200 && code < 300;
        http.end();
      }
      if (!delivered) vTaskDelay(pdMS_TO_TICKS(1000U << attempt));
    }
    // Failed events are intentionally dropped after bounded retries to protect RAM.
  }
}

void telemetryBegin() {
  telemetryPrefs.begin("hidtele", false);
  telemetryUrl = telemetryPrefs.getString("url", "");
  telemetryKey = telemetryPrefs.getString("key", "");
  telemetryOn = telemetryPrefs.getBool("enabled", false);
  telemetryQueue = xQueueCreate(24, sizeof(TelemetryEvent));
  if (telemetryQueue) xTaskCreatePinnedToCore(telemetryTask, "hid-telemetry", 6144, nullptr, 1, nullptr, 0);
}
