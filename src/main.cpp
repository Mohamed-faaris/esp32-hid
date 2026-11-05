#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <BleKeyboard.h>
#include "config.h"
#include "telemetry.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define HID_API_SECRET "change-this-secret-before-use"
#endif
#ifndef HID_AP_SSID
#define HID_AP_SSID "ESP32-HID"
#endif
#ifndef HID_AP_PASSWORD
#define HID_AP_PASSWORD "change-this-ap-password"
#endif
#ifndef HID_STA_SSID
#define HID_STA_SSID ""
#endif
#ifndef HID_STA_PASSWORD
#define HID_STA_PASSWORD ""
#endif

enum class LogLevel : uint8_t { NONE, ERROR, WARN, INFO, DEBUG, TRACE };
enum class CmdType : uint8_t { TYPE, PRESS, HOTKEY, SLEEP };
struct Command {
  CmdType type;
  char value[config::MAX_TEXT_LENGTH + 1];
  uint32_t delayMs;
  uint16_t jitterMs;
  bool instant;
};

static BleKeyboard keyboard("ESP32-HID", "Faaris", 100);
static WebServer http(80);
static WebSocketsServer websocket(81);
static Preferences preferences;
static QueueHandle_t queueHandle;
static volatile bool paused = false;
static volatile bool stopRequested = false;
static LogLevel currentLogLevel = LogLevel::INFO;
static uint32_t charDelayMs = config::DEFAULT_KEY_INTERVAL_MS;
static uint16_t jitterMs = 0;
static String stationSsid, stationPassword;
static uint32_t lastReconnect = 0;

static const char* levelName(LogLevel level) {
  switch (level) {
    case LogLevel::ERROR: return "ERROR";
    case LogLevel::WARN: return "WARN";
    case LogLevel::INFO: return "INFO";
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::TRACE: return "TRACE";
    default: return "NONE";
  }
}
static void logMessage(LogLevel level, const String& message) {
  if (level == LogLevel::NONE || level > currentLogLevel) return;
  String line = String("[") + levelName(level) + "] " + message;
  Serial.println(line);
  websocket.broadcastTXT(line);
  telemetryPublish("device_log", message.c_str());
}
static bool authorized() {
  String received = http.header("Authorization");
  String expected = String("Bearer ") + HID_API_SECRET;
  return received.length() == expected.length() && received.equals(expected);
}
static bool checkAuth() {
  if (authorized()) return true;
  http.send(401, "application/json", "{\"error\":\"unauthorized\"}");
  return false;
}
static bool queueCommand(const Command& command) {
  return queueHandle && xQueueSend(queueHandle, &command, 0) == pdTRUE;
}
static void clearQueue() {
  if (!queueHandle) return;
  Command ignored;
  while (xQueueReceive(queueHandle, &ignored, 0) == pdTRUE) {}
}
static uint8_t keyCode(String key) {
  key.toUpperCase();
  if (key == "ENTER" || key == "RETURN") return KEY_RETURN;
  if (key == "ESC" || key == "ESCAPE") return KEY_ESC;
  if (key == "BACKSPACE") return KEY_BACKSPACE;
  if (key == "TAB") return KEY_TAB;
  if (key == "SPACE") return ' ';
  if (key == "UP") return KEY_UP_ARROW;
  if (key == "DOWN") return KEY_DOWN_ARROW;
  if (key == "LEFT") return KEY_LEFT_ARROW;
  if (key == "RIGHT") return KEY_RIGHT_ARROW;
  if (key == "DELETE") return KEY_DELETE;
  if (key == "HOME") return KEY_HOME;
  if (key == "END") return KEY_END;
  if (key == "PAGEUP") return KEY_PAGE_UP;
  if (key == "PAGEDOWN") return KEY_PAGE_DOWN;
  if (key == "CTRL" || key == "CONTROL") return KEY_LEFT_CTRL;
  if (key == "SHIFT") return KEY_LEFT_SHIFT;
  if (key == "ALT") return KEY_LEFT_ALT;
  if (key == "GUI" || key == "WIN" || key == "CMD") return KEY_LEFT_GUI;
  if (key.length() == 1) return static_cast<uint8_t>(key[0]);
  if (key.length() >= 2 && key[0] == 'F') {
    int number = key.substring(1).toInt();
    if (number >= 1 && number <= 12) return static_cast<uint8_t>(KEY_F1 + number - 1);
  }
  return 0;
}
static void sendHotkey(const String& expression) {
  uint8_t codes[6]; size_t count = 0; int start = 0;
  while (start < static_cast<int>(expression.length()) && count < 6) {
    int separator = expression.indexOf('+', start);
    String part = expression.substring(start, separator < 0 ? expression.length() : separator);
    part.trim(); uint8_t code = keyCode(part);
    if (!code) { logMessage(LogLevel::WARN, "Rejected unknown hotkey token"); return; }
    codes[count++] = code;
    if (separator < 0) break;
    start = separator + 1;
  }
  for (size_t i = 0; i < count; ++i) keyboard.press(codes[i]);
  vTaskDelay(pdMS_TO_TICKS(35));
  keyboard.releaseAll();
}
static void waitInterruptible(uint32_t milliseconds) {
  uint32_t elapsed = 0;
  while (elapsed < milliseconds && !stopRequested) {
    while (paused && !stopRequested) vTaskDelay(pdMS_TO_TICKS(25));
    uint32_t part = min(static_cast<uint32_t>(25), milliseconds - elapsed);
    vTaskDelay(pdMS_TO_TICKS(part)); elapsed += part;
  }
}
static void executorTask(void*) {
  Command command;
  for (;;) {
    if (stopRequested) {
      keyboard.releaseAll();
      stopRequested = false;
      clearQueue();
      logMessage(LogLevel::WARN, "Queue stopped and cleared");
    }
    if (paused) { vTaskDelay(pdMS_TO_TICKS(30)); continue; }
    if (xQueueReceive(queueHandle, &command, pdMS_TO_TICKS(100)) != pdTRUE) continue;
    if (!keyboard.isConnected()) { logMessage(LogLevel::WARN, "Command skipped: no BLE host connected"); continue; }
    switch (command.type) {
      case CmdType::SLEEP: waitInterruptible(command.delayMs); break;
      case CmdType::PRESS: {
        uint8_t code = keyCode(command.value);
        if (code) {
          keyboard.press(code); vTaskDelay(pdMS_TO_TICKS(35)); keyboard.release(code);
          logMessage(LogLevel::INFO, "HID key event sent");
        } else logMessage(LogLevel::WARN, "Unknown key name");
        break;
      }
      case CmdType::HOTKEY:
        sendHotkey(command.value); logMessage(LogLevel::INFO, "HID hotkey sent"); break;
      case CmdType::TYPE:
        for (size_t i = 0; command.value[i] && !stopRequested; ++i) {
          while (paused && !stopRequested) vTaskDelay(pdMS_TO_TICKS(25));
          keyboard.write(static_cast<uint8_t>(command.value[i]));
          if (!command.instant) {
            uint32_t delay = command.delayMs;
            if (command.jitterMs) delay += random(0, command.jitterMs + 1);
            waitInterruptible(delay);
          }
        }
        logMessage(LogLevel::INFO, "HID text command completed"); break;
    }
    if (stopRequested) {
      keyboard.releaseAll(); stopRequested = false;
      logMessage(LogLevel::WARN, "Stopped; all keys released");
    }
  }
}
static bool parseCommand(JsonDocument& json, Command& command) {
  const char* type = json["type"] | "";
  const char* value = json["value"] | "";
  if (strlen(value) > config::MAX_TEXT_LENGTH) return false;
  strlcpy(command.value, value, sizeof(command.value));
  command.delayMs = json["delay_ms"] | charDelayMs;
  command.jitterMs = json["jitter_ms"] | jitterMs;
  command.instant = json["instant"] | false;
  if (!strcmp(type, "type")) command.type = CmdType::TYPE;
  else if (!strcmp(type, "press")) command.type = CmdType::PRESS;
  else if (!strcmp(type, "hotkey")) command.type = CmdType::HOTKEY;
  else if (!strcmp(type, "sleep")) {
    command.type = CmdType::SLEEP; command.delayMs = json["ms"] | 0;
    if (command.delayMs > 60000) return false;
  } else return false;
  return command.delayMs <= 5000 && command.jitterMs <= config::MAX_RANDOM_JITTER_MS;
}
static void sendError(int status, const char* error) {
  JsonDocument json; json["error"] = error; String body; serializeJson(json, body);
  http.send(status, "application/json", body);
}
static void statusRoute() {
  JsonDocument json;
  json["device"] = "ESP32-HID"; json["ble_connected"] = keyboard.isConnected();
  json["wifi_mode"] = "AP+STA"; json["ap_ip"] = WiFi.softAPIP().toString();
  json["sta_connected"] = WiFi.status() == WL_CONNECTED;
  json["sta_ip"] = WiFi.localIP().toString();
  json["queue_depth"] = uxQueueMessagesWaiting(queueHandle);
  json["queue_paused"] = paused; json["log_level"] = levelName(currentLogLevel);
  String body; serializeJson(json, body); http.send(200, "application/json", body);
}
static void commandRoute() {
  if (!checkAuth()) return;
  JsonDocument json;
  if (deserializeJson(json, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  Command command{};
  if (!parseCommand(json, command)) { sendError(400, "invalid_command_or_limits"); return; }
  if (!queueCommand(command)) { sendError(429, "queue_full"); return; }
  http.send(202, "application/json", "{\"queued\":true}");
}
static void sequenceRoute() {
  if (!checkAuth()) return;
  JsonDocument json;
  if (deserializeJson(json, http.arg("plain")) || !json["commands"].is<JsonArray>()) {
    sendError(400, "invalid_sequence"); return;
  }
  JsonArray items = json["commands"].as<JsonArray>();
  if (!items.size() || items.size() > 20 || uxQueueSpacesAvailable(queueHandle) < items.size()) {
    sendError(429, "invalid_or_queue_full"); return;
  }
  Command parsed[20]; size_t count = 0;
  for (JsonVariant item : items) {
    JsonDocument one; one.set(item);
    if (!parseCommand(one, parsed[count])) { sendError(400, "invalid_sequence_item"); return; }
    ++count;
  }
  for (size_t i = 0; i < count; ++i) queueCommand(parsed[i]);
  http.send(202, "application/json", "{\"queued\":true}");
}
static void queueAction(const char* action) {
  if (!checkAuth()) return;
  if (!strcmp(action, "pause")) paused = true;
  else if (!strcmp(action, "resume")) paused = false;
  else { stopRequested = true; paused = false; clearQueue(); keyboard.releaseAll(); }
  logMessage(LogLevel::INFO, String("Queue action: ") + action);
  http.send(200, "application/json", "{\"ok\":true}");
}
static void logConfigRoute() {
  if (!checkAuth()) return;
  String value = http.arg("plain"); value.trim(); value.toUpperCase();
  if (value == "NONE") currentLogLevel = LogLevel::NONE;
  else if (value == "ERROR") currentLogLevel = LogLevel::ERROR;
  else if (value == "WARN") currentLogLevel = LogLevel::WARN;
  else if (value == "INFO") currentLogLevel = LogLevel::INFO;
  else if (value == "DEBUG") currentLogLevel = LogLevel::DEBUG;
  else if (value == "TRACE") currentLogLevel = LogLevel::TRACE;
  else { sendError(400, "invalid_level"); return; }
  preferences.putUChar("logLevel", static_cast<uint8_t>(currentLogLevel));
  http.send(200, "application/json", "{\"ok\":true}");
}
static void typingConfigRoute() {
  if (!checkAuth()) return;
  JsonDocument json;
  if (deserializeJson(json, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  uint32_t delay = json["delay_ms"] | charDelayMs;
  uint16_t jitter = json["jitter_ms"] | jitterMs;
  if (delay > 1000 || jitter > config::MAX_RANDOM_JITTER_MS) { sendError(400, "out_of_range"); return; }
  charDelayMs = delay; jitterMs = jitter;
  preferences.putUInt("typeDelay", delay); preferences.putUShort("jitter", jitter);
  http.send(200, "application/json", "{\"ok\":true}");
}
static void wifiConfigRoute() {
  if (!checkAuth()) return;
  JsonDocument json;
  if (deserializeJson(json, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  String ssid = json["ssid"] | "";
  String password = json["password"] | "";
  if (ssid.length() > 32 || password.length() > 63 || (ssid.length() && password.length() < 8)) {
    sendError(400, "invalid_wifi_credentials"); return;
  }
  stationSsid = ssid;
  stationPassword = password;
  preferences.putString("staSsid", stationSsid);
  preferences.putString("staPass", stationPassword);
  WiFi.disconnect(false, false);
  if (stationSsid.length()) WiFi.begin(stationSsid.c_str(), stationPassword.c_str());
  http.send(200, "application/json", "{\"ok\":true,\"mode\":\"AP+STA\"}");
}

static void telemetryConfigRoute() {
  if (!checkAuth()) return;
  JsonDocument json;
  if (deserializeJson(json, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  String endpoint = json["endpoint"] | "";
  String apiKey = json["api_key"] | "";
  bool enabled = json["enabled"] | false;
  if (!telemetryConfigure(endpoint, apiKey, enabled)) { sendError(400, "https_endpoint_and_key_required"); return; }
  http.send(200, "application/json", "{\"ok\":true,\"tls\":\"requires HID_TELEMETRY_ROOT_CA\"}");
}
static void setupRoutes() {
  const char* headers[] = {"Authorization"}; http.collectHeaders(headers, 1);
  http.on("/api/status", HTTP_GET, statusRoute);
  http.on("/api/keyboard/type", HTTP_POST, commandRoute);
  http.on("/api/keyboard/press", HTTP_POST, commandRoute);
  http.on("/api/keyboard/hotkey", HTTP_POST, commandRoute);
  http.on("/api/keyboard/sleep", HTTP_POST, commandRoute);
  http.on("/api/keyboard/sequence", HTTP_POST, sequenceRoute);
  http.on("/api/queue/pause", HTTP_POST, [](){ queueAction("pause"); });
  http.on("/api/queue/resume", HTTP_POST, [](){ queueAction("resume"); });
  http.on("/api/queue/stop", HTTP_POST, [](){ queueAction("stop"); });
  http.on("/api/logs/config", HTTP_POST, logConfigRoute);
  http.on("/api/keyboard/config", HTTP_POST, typingConfigRoute);
  http.on("/api/telemetry/config", HTTP_POST, telemetryConfigRoute);
  http.on("/api/wifi/config", HTTP_POST, wifiConfigRoute);
  http.on("/api/bluetooth/status", HTTP_GET, [](){
    JsonDocument json; json["connected"] = keyboard.isConnected();
    json["role"] = "BLE HID peripheral";
    json["pairing"] = "Start pairing from the computer Bluetooth settings";
    String body; serializeJson(json, body); http.send(200, "application/json", body);
  });
  http.onNotFound([](){ sendError(404, "not_found"); });
}
static void serialCli() {
  static String line;
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      line.trim();
      if (line == "help") Serial.println("help | status | pause | resume | stop | type <text> | press <key> | hotkey <A+B>");
      else if (line == "status") Serial.printf("BLE=%s queue=%u paused=%s\n", keyboard.isConnected() ? "connected" : "disconnected", static_cast<unsigned>(uxQueueMessagesWaiting(queueHandle)), paused ? "yes" : "no");
      else if (line == "pause") paused = true;
      else if (line == "resume") paused = false;
      else if (line == "stop") { stopRequested = true; paused = false; clearQueue(); keyboard.releaseAll(); }
      else {
        Command command{}; bool valid = false;
        if (line.startsWith("type ")) {
          command.type = CmdType::TYPE; strlcpy(command.value, line.substring(5).c_str(), sizeof(command.value));
          command.delayMs = charDelayMs; command.jitterMs = jitterMs; valid = true;
        } else if (line.startsWith("press ")) {
          command.type = CmdType::PRESS; strlcpy(command.value, line.substring(6).c_str(), sizeof(command.value)); valid = true;
        } else if (line.startsWith("hotkey ")) {
          command.type = CmdType::HOTKEY; strlcpy(command.value, line.substring(7).c_str(), sizeof(command.value)); valid = true;
        }
        if (valid && !queueCommand(command)) Serial.println("ERR queue full");
        else if (!valid && line.length()) Serial.println("ERR unknown command");
      }
      line = "";
    } else if (line.length() < config::MAX_TEXT_LENGTH) line += c;
  }
}
void setup() {
  Serial.begin(config::SERIAL_BAUD); delay(200); randomSeed(esp_random());
  preferences.begin("esp32hid", false);
  currentLogLevel = static_cast<LogLevel>(preferences.getUChar("logLevel", static_cast<uint8_t>(LogLevel::INFO)));
  charDelayMs = preferences.getUInt("typeDelay", config::DEFAULT_KEY_INTERVAL_MS);
  jitterMs = preferences.getUShort("jitter", 0);
  stationSsid = preferences.getString("staSsid", HID_STA_SSID);
  stationPassword = preferences.getString("staPass", HID_STA_PASSWORD);
  queueHandle = xQueueCreate(config::COMMAND_QUEUE_CAPACITY, sizeof(Command));
  telemetryBegin();
  keyboard.begin();
  WiFi.mode(WIFI_AP_STA); WiFi.softAP(HID_AP_SSID, HID_AP_PASSWORD);
  if (stationSsid.length()) WiFi.begin(stationSsid.c_str(), stationPassword.c_str());
  setupRoutes(); http.begin(); websocket.begin();
  xTaskCreatePinnedToCore(executorTask, "hid-executor", 6144, nullptr, 1, nullptr, 1);
  logMessage(LogLevel::INFO, String("AP ready at ") + WiFi.softAPIP().toString());
}
void loop() {
  http.handleClient(); websocket.loop(); serialCli();
  if (stationSsid.length() && WiFi.status() != WL_CONNECTED && millis() - lastReconnect > 15000) {
    lastReconnect = millis(); WiFi.reconnect();
  }
  delay(2);
}
