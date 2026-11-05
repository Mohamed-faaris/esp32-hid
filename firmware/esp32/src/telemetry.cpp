#include "telemetry.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config.h"
#include "secrets.h"

struct TelemetryEvent {
  char name[32];
  char detail[72];
  uint32_t uptime;
};

static QueueHandle_t telemetryQueue = nullptr;
static Preferences telemetryPrefs;
static String telemetryUrl;
static String telemetryKey;
static bool telemetryEnabled = false;

bool telemetryConfigure(const String& endpoint, const String& apiKey, bool enabled) {
  if (endpoint.length() && !endpoint.startsWith("https://")) return false;
  if (enabled && (!apiKey.length() || strlen(HID_TELEMETRY_ROOT_CA) < 100)) return false;
  telemetryUrl = endpoint;
  telemetryKey = apiKey;
  telemetryEnabled = enabled && endpoint.length() && apiKey.length();
  telemetryPrefs.putString("url", telemetryUrl);
  telemetryPrefs.putString("key", telemetryKey);
  telemetryPrefs.putBool("enabled", telemetryEnabled);
  return true;
}

void telemetryPublish(const char* eventName, const char* detail) {
  if (!telemetryEnabled || !telemetryQueue) return;
  TelemetryEvent event{};
  strlcpy(event.name, eventName, sizeof(event.name));
  strlcpy(event.detail, detail, sizeof(event.detail));
  event.uptime = millis();
  xQueueSend(telemetryQueue, &event, 0);
}

static void telemetryTask(void*) {
  TelemetryEvent event;
  for (;;) {
    if (!telemetryEnabled || WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (xQueueReceive(telemetryQueue, &event, pdMS_TO_TICKS(1000)) != pdTRUE) continue;

    JsonDocument body;
    body["device"] = "ESP32-HID";
    body["event"] = event.name;
    body["detail"] = event.detail;
    body["uptime_ms"] = event.uptime;
    String payload;
    serializeJson(body, payload);

    bool sent = false;
    for (uint8_t attempt = 0; attempt < 3 && !sent; ++attempt) {
      WiFiClientSecure tls;
      tls.setCACert(HID_TELEMETRY_ROOT_CA);
      HTTPClient client;
      if (client.begin(tls, telemetryUrl)) {
        client.addHeader("Content-Type", "application/json");
        client.addHeader("Authorization", String("Bearer ") + telemetryKey);
        const int status = client.POST(payload);
        sent = status >= 200 && status < 300;
        client.end();
      }
      if (!sent) vTaskDelay(pdMS_TO_TICKS(750U << attempt));
    }
    // Failed events are dropped after bounded retries to protect limited RAM.
  }
}

void telemetryBegin() {
  telemetryPrefs.begin("hidtele", false);
  telemetryUrl = telemetryPrefs.getString("url", "");
  telemetryKey = telemetryPrefs.getString("key", "");
  telemetryEnabled = telemetryPrefs.getBool("enabled", false)
    && telemetryUrl.startsWith("https://")
    && telemetryKey.length() > 0
    && strlen(HID_TELEMETRY_ROOT_CA) >= 100;
  telemetryQueue = xQueueCreate(24, sizeof(TelemetryEvent));
  if (telemetryQueue) {
    xTaskCreatePinnedToCore(telemetryTask, "hid-telemetry", 6144, nullptr, 1, nullptr, 0);
  }
}
