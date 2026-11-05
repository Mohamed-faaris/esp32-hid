#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <EspBle.h>
#include "config.h"
#include "telemetry.h"
#include "ota.h"
#include "secrets.h"

enum class LogLevel : uint8_t { NONE, ERROR, WARN, INFO, DEBUG, TRACE };
enum class CommandType : uint8_t { TYPE, KEY, HOTKEY, SLEEP, MOUSE_MOVE, MOUSE_CLICK, MOUSE_WHEEL };

struct Command {
  CommandType type = CommandType::SLEEP;
  char value[config::MAX_TEXT_LENGTH + 1] = {};
  int16_t x = 0;
  int16_t y = 0;
  int16_t wheel = 0;
  uint32_t delayMs = config::DEFAULT_KEY_INTERVAL_MS;
  uint16_t jitterMs = 0;
  uint8_t button = ESP_BLE_HID_MOUSE_LEFT;
  bool instant = false;
};

static EspBle ble;
static WebServer http(80);
static WebSocketsServer websocket(81);
static Preferences prefs;
static QueueHandle_t commandQueue = nullptr;
static bool queuePaused = false;
static bool bleInitialized = false;
static bool apStarted = false;
static bool stopRequested = false;
static bool commandActive = false;
static bool sleepStarted = false;
static bool recording = false;
static bool replayingMacro = false;
static bool websocketAuthenticated[32] = {};
static LogLevel logLevel = LogLevel::INFO;
static String modeName = "NORMAL";
static String staSsid, staPassword;
static String macroName, macroJson;
static uint8_t macroCount = 0;
static bool macroOverflow = false;
static Command activeCommand{};
static size_t textIndex = 0;
static uint32_t nextActionAt = 0;
static uint32_t pausedAt = 0;
static uint32_t activeStartedAt = 0;
static uint32_t lastReconnectAt = 0;
static String logRing[config::LOG_RING_CAPACITY];
static size_t logWriteIndex = 0;
static size_t logCount = 0;
static uint32_t typingDelayMs = config::DEFAULT_KEY_INTERVAL_MS;
static uint16_t typingJitterMs = 0;

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

static const char* commandName(CommandType type) {
  switch (type) {
    case CommandType::TYPE: return "type";
    case CommandType::KEY: return "press";
    case CommandType::HOTKEY: return "hotkey";
    case CommandType::SLEEP: return "sleep";
    case CommandType::MOUSE_MOVE: return "mouse_move";
    case CommandType::MOUSE_CLICK: return "mouse_click";
    case CommandType::MOUSE_WHEEL: return "mouse_wheel";
  }
  return "unknown";
}

static void sendLogToAuthenticatedClients(const String& line) {
  for (uint8_t id = 0; id < 32; ++id) {
    if (websocketAuthenticated[id]) {
      String payload = line;
      websocket.sendTXT(id, payload);
    }
  }
}

static void logEvent(LogLevel level, const String& message) {
  if (level == LogLevel::NONE || level > logLevel) return;
  const String line = String("[") + levelName(level) + "] " + message;
  logRing[logWriteIndex] = line;
  logWriteIndex = (logWriteIndex + 1) % config::LOG_RING_CAPACITY;
  if (logCount < config::LOG_RING_CAPACITY) ++logCount;
  Serial.println(line);
  sendLogToAuthenticatedClients(line);
  telemetryPublish("device_log", message.c_str());
}

static bool authorized() {
  const String received = http.header("Authorization");
  const String expected = String("Bearer ") + HID_API_SECRET;
  return received.length() == expected.length() && received.equals(expected);
}

static bool requireAuth(bool allowDuringOta = false) {
  if (!authorized()) {
    http.send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return false;
  }
  if (otaIsBusy() && !allowDuringOta) {
    http.send(503, "application/json", "{\"error\":\"ota_in_progress\"}");
    return false;
  }
  return true;
}

static void sendJson(int status, JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  http.send(status, "application/json", body);
}

static void sendError(int status, const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  sendJson(status, doc);
}

static bool safeMacroName(const String& name) {
  if (!name.length() || name.length() > 14) return false;
  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name[i];
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) return false;
  }
  return true;
}

static uint8_t mouseButton(const String& button) {
  if (button == "left") return ESP_BLE_HID_MOUSE_LEFT;
  if (button == "right") return ESP_BLE_HID_MOUSE_RIGHT;
  if (button == "middle") return ESP_BLE_HID_MOUSE_MIDDLE;
  if (button == "back") return ESP_BLE_HID_MOUSE_BACK;
  if (button == "forward") return 0x10;
  return 0;
}

static bool parseCommand(JsonVariantConst json, Command& command) {
  const char* type = json["type"] | "";
  command = Command{};

  if (!strcmp(type, "type")) {
    command.type = CommandType::TYPE;
    const char* value = json["value"] | "";
    if (strlen(value) > config::MAX_TEXT_LENGTH) return false;
    strlcpy(command.value, value, sizeof(command.value));
    command.delayMs = json["delay_ms"] | typingDelayMs;
    command.jitterMs = json["jitter_ms"] | typingJitterMs;
    command.instant = json["instant"] | (modeName == "INSTANT");
    if (modeName == "INSTANT") command.instant = true;
    if (modeName == "RANDOMIZED" && command.jitterMs == 0) command.jitterMs = 50;
    return command.delayMs <= 1000 && command.jitterMs <= config::MAX_RANDOM_JITTER_MS;
  }

  if (!strcmp(type, "press") || !strcmp(type, "key")) {
    command.type = CommandType::KEY;
    const char* value = json["value"] | "";
    if (!value[0] || strlen(value) > 16) return false;
    strlcpy(command.value, value, sizeof(command.value));
    return true;
  }

  if (!strcmp(type, "hotkey")) {
    command.type = CommandType::HOTKEY;
    const char* value = json["value"] | "";
    if (!value[0] || strlen(value) > 48) return false;
    strlcpy(command.value, value, sizeof(command.value));
    return true;
  }

  if (!strcmp(type, "sleep")) {
    command.type = CommandType::SLEEP;
    command.delayMs = json["ms"] | 0;
    return command.delayMs <= 60000;
  }

  if (!strcmp(type, "mouse_move")) {
    command.type = CommandType::MOUSE_MOVE;
    command.x = json["dx"] | 0;
    command.y = json["dy"] | 0;
    command.wheel = json["wheel"] | 0;
    return abs(command.x) <= config::MAX_MOUSE_DELTA
      && abs(command.y) <= config::MAX_MOUSE_DELTA
      && abs(command.wheel) <= 100;
  }

  if (!strcmp(type, "mouse_click")) {
    command.type = CommandType::MOUSE_CLICK;
    const char* button = json["button"] | "left";
    command.button = mouseButton(button);
    return command.button != 0;
  }

  if (!strcmp(type, "mouse_wheel")) {
    command.type = CommandType::MOUSE_WHEEL;
    command.wheel = json["delta"] | 0;
    return abs(command.wheel) <= 100;
  }

  return false;
}

static bool appendMacroAction(const Command& command) {
  if (!recording || replayingMacro) return true;
  if (macroCount >= config::MAX_MACRO_COMMANDS || macroJson.length() > 2500) {
    macroOverflow = true;
    return false;
  }

  JsonDocument item;
  item["type"] = commandName(command.type);
  if (command.type == CommandType::TYPE || command.type == CommandType::KEY || command.type == CommandType::HOTKEY) {
    item["value"] = command.value;
  }
  if (command.type == CommandType::SLEEP) item["ms"] = command.delayMs;
  if (command.type == CommandType::MOUSE_MOVE) {
    item["dx"] = command.x;
    item["dy"] = command.y;
    item["wheel"] = command.wheel;
  }
  if (command.type == CommandType::MOUSE_CLICK) {
    if (command.button == ESP_BLE_HID_MOUSE_LEFT) item["button"] = "left";
    else if (command.button == ESP_BLE_HID_MOUSE_RIGHT) item["button"] = "right";
    else if (command.button == ESP_BLE_HID_MOUSE_MIDDLE) item["button"] = "middle";
    else if (command.button == ESP_BLE_HID_MOUSE_BACK) item["button"] = "back";
    else item["button"] = "forward";
  }
  if (command.type == CommandType::MOUSE_WHEEL) item["delta"] = command.wheel;
  if (command.type == CommandType::TYPE) {
    item["delay_ms"] = command.delayMs;
    item["jitter_ms"] = command.jitterMs;
    item["instant"] = command.instant;
  }

  String encoded;
  serializeJson(item, encoded);
  if (macroJson.length() + encoded.length() + 2 > 3000) {
    macroOverflow = true;
    return false;
  }
  if (macroCount) macroJson += ",";
  macroJson += encoded;
  ++macroCount;
  return true;
}

static bool appendMacroBatch(const Command* commands, size_t count) {
  if (!recording || replayingMacro) return true;
  const String originalJson = macroJson;
  const uint8_t originalCount = macroCount;
  for (size_t i = 0; i < count; ++i) {
    if (!appendMacroAction(commands[i])) {
      macroJson = originalJson;
      macroCount = originalCount;
      macroOverflow = true;
      return false;
    }
  }
  return true;
}

static bool enqueue(const Command& command, bool shouldRecord = true) {
  if (!commandQueue || uxQueueSpacesAvailable(commandQueue) == 0) return false;
  if (shouldRecord && recording && !replayingMacro) {
    if (!appendMacroAction(command)) return false;
  }
  return xQueueSend(commandQueue, &command, 0) == pdTRUE;
}

static void clearQueue() {
  if (!commandQueue) return;
  Command ignored;
  while (xQueueReceive(commandQueue, &ignored, 0) == pdTRUE) {}
}

static bool sendKey(const String& key) {
  if (key.length() == 1) return ble.hidKeyboard().tapKey(key[0]);
  String normalized = key;
  normalized.toUpperCase();
  const uint8_t special[] = {
    0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E,
    0x4F, 0x50, 0x51, 0x52
  };
  const char* names[] = {
    "ENTER", "ESC", "BACKSPACE", "TAB", "SPACE", "HOME", "PAGEUP", "DELETE",
    "END", "PAGEDOWN", "RIGHT", "LEFT", "DOWN", "UP"
  };
  for (size_t i = 0; i < sizeof(special); ++i) {
    if (normalized == names[i]) return ble.hidKeyboard().tapUsage(special[i]);
  }
  if (normalized.length() >= 2 && normalized[0] == 'F') {
    const int number = normalized.substring(1).toInt();
    if (number >= 1 && number <= 12) return ble.hidKeyboard().tapUsage(static_cast<uint8_t>(0x39 + number));
  }
  return false;
}

static bool sendHotkey(const String& expression) {
  uint8_t modifiers = 0;
  uint8_t usage = 0;
  int start = 0;
  while (start < static_cast<int>(expression.length())) {
    const int separator = expression.indexOf('+', start);
    String part = expression.substring(start, separator < 0 ? expression.length() : separator);
    part.trim();
    if (!part.length()) return false;
    String upper = part;
    upper.toUpperCase();
    if (upper == "CTRL" || upper == "CONTROL") modifiers |= 0x01;
    else if (upper == "SHIFT") modifiers |= 0x02;
    else if (upper == "ALT") modifiers |= 0x04;
    else if (upper == "GUI" || upper == "WIN" || upper == "CMD") modifiers |= 0x08;
    else if (part.length() == 1) {
      if (usage != 0) return false;
      const char c = part[0];
      if (c >= 'a' && c <= 'z') usage = static_cast<uint8_t>(0x04 + c - 'a');
      else if (c >= 'A' && c <= 'Z') { usage = static_cast<uint8_t>(0x04 + c - 'A'); modifiers |= 0x02; }
      else if (c >= '1' && c <= '9') usage = static_cast<uint8_t>(0x1E + c - '1');
      else if (c == '0') usage = 0x27;
      else return false;
    } else {
      if (usage != 0) return false;
      String key = part;
      key.toUpperCase();
      if (key == "ENTER") usage = 0x28;
      else if (key == "ESC" || key == "ESCAPE") usage = 0x29;
      else if (key == "BACKSPACE") usage = 0x2A;
      else if (key == "TAB") usage = 0x2B;
      else if (key == "SPACE") usage = 0x2C;
      else if (key == "HOME") usage = 0x4A;
      else if (key == "PAGEUP") usage = 0x4B;
      else if (key == "DELETE" || key == "DEL") usage = 0x4C;
      else if (key == "END") usage = 0x4D;
      else if (key == "PAGEDOWN") usage = 0x4E;
      else if (key == "RIGHT") usage = 0x4F;
      else if (key == "LEFT") usage = 0x50;
      else if (key == "DOWN") usage = 0x51;
      else if (key == "UP") usage = 0x52;
      else if (key.length() >= 2 && key[0] == 'F') {
        for (size_t i = 1; i < key.length(); ++i) {
          if (!isDigit(key[i])) return false;
        }
        const int number = key.substring(1).toInt();
        if (number >= 1 && number <= 12) usage = static_cast<uint8_t>(0x39 + number);
        else return false;
      } else return false;
    }
    if (separator < 0) break;
    start = separator + 1;
  }
  if (expression.endsWith("+")) return false;
  return usage != 0 && ble.hidKeyboard().tapUsage(usage, modifiers);
}

static bool recordStart(const String& name) {
  if (!safeMacroName(name) || recording) return false;
  macroName = name;
  macroJson = "";
  macroCount = 0;
  macroOverflow = false;
  recording = true;
  logEvent(LogLevel::INFO, "Explicit command recording started");
  return true;
}

static bool recordStop() {
  if (!recording) return false;
  recording = false;
  bool saved = false;
  if (macroCount && !macroOverflow) {
    const String serialized = String("{\"commands\":[") + macroJson + "]}";
    if (prefs.putString(macroName.c_str(), serialized) > 0) {
      String names = prefs.getString("macroNames", "");
      bool alreadyListed = false;
      int start = 0;
      while (start < static_cast<int>(names.length())) {
        const int end = names.indexOf('|', start);
        const String existing = names.substring(start, end < 0 ? names.length() : end);
        if (existing == macroName) alreadyListed = true;
        if (end < 0) break;
        start = end + 1;
      }
      if (alreadyListed) {
        saved = true;
      } else {
        const size_t addedLength = (names.length() ? 1 : 0) + macroName.length();
        if (names.length() + addedLength <= 180) {
          if (names.length()) names += "|";
          names += macroName;
          saved = prefs.putString("macroNames", names) > 0;
        }
        if (!saved) prefs.remove(macroName.c_str());
      }
    }
  }
  logEvent(saved ? LogLevel::INFO : LogLevel::WARN,
    saved ? "Command recording stopped and macro saved"
          : (macroOverflow ? "Macro exceeded storage limits or index capacity"
                           : "Macro could not be saved"));
  return saved;
}

static void startPause() {
  if (!queuePaused) {
    queuePaused = true;
    pausedAt = millis();
  }
}
static void resumeQueue() {
  if (queuePaused) {
    if (commandActive) nextActionAt += millis() - pausedAt;
    queuePaused = false;
  }
}

static void processQueue() {
  if (stopRequested) {
    stopRequested = false;
    queuePaused = false;
    commandActive = false;
    sleepStarted = false;
    clearQueue();
    ble.hidKeyboard().releaseAll();
    ble.hidMouse().releaseAll();
    logEvent(LogLevel::WARN, "Queue stopped, cleared and HID controls released");
    return;
  }
  if (queuePaused) return;

  if (!commandActive) {
    if (xQueueReceive(commandQueue, &activeCommand, 0) != pdTRUE) return;
    commandActive = true;
    sleepStarted = false;
    textIndex = 0;
    activeStartedAt = millis();
    nextActionAt = activeStartedAt;
  }

  const bool needsKeyboard = activeCommand.type == CommandType::TYPE
    || activeCommand.type == CommandType::KEY
    || activeCommand.type == CommandType::HOTKEY;
  const bool needsMouse = activeCommand.type == CommandType::MOUSE_MOVE
    || activeCommand.type == CommandType::MOUSE_CLICK
    || activeCommand.type == CommandType::MOUSE_WHEEL;

  if (needsKeyboard && !ble.hidKeyboard().ready()) {
    if (millis() - activeStartedAt > 15000) {
      logEvent(LogLevel::WARN, "Keyboard command expired: no paired host ready");
      commandActive = false;
    }
    return;
  }
  if (needsMouse && !ble.hidMouse().ready()) {
    if (millis() - activeStartedAt > 15000) {
      logEvent(LogLevel::WARN, "Mouse command expired: no paired host ready");
      commandActive = false;
    }
    return;
  }

  if (static_cast<int32_t>(millis() - nextActionAt) < 0) return;
  bool completed = false;
  bool success = true;

  switch (activeCommand.type) {
    case CommandType::SLEEP:
      if (!sleepStarted) {
        sleepStarted = true;
        nextActionAt = millis() + activeCommand.delayMs;
        return;
      }
      completed = true;
      break;
    case CommandType::TYPE:
      if (activeCommand.value[textIndex] == '\0') { completed = true; break; }
      success = ble.hidKeyboard().tapKey(activeCommand.value[textIndex]);
      ++textIndex;
      if (activeCommand.value[textIndex] != '\0') {
        uint32_t delayMs = activeCommand.instant ? 0 : activeCommand.delayMs;
        if (activeCommand.jitterMs) delayMs += random(0, activeCommand.jitterMs + 1);
        nextActionAt = millis() + delayMs;
      } else completed = true;
      break;
    case CommandType::KEY:
      success = sendKey(activeCommand.value);
      completed = true;
      break;
    case CommandType::HOTKEY:
      success = sendHotkey(activeCommand.value);
      completed = true;
      break;
    case CommandType::MOUSE_MOVE:
      success = ble.hidMouse().move(
        static_cast<int8_t>(activeCommand.x),
        static_cast<int8_t>(activeCommand.y),
        static_cast<int8_t>(activeCommand.wheel)
      );
      completed = true;
      break;
    case CommandType::MOUSE_CLICK:
      success = ble.hidMouse().click(activeCommand.button);
      completed = true;
      break;
    case CommandType::MOUSE_WHEEL:
      success = ble.hidMouse().wheel(static_cast<int8_t>(activeCommand.wheel));
      completed = true;
      break;
  }

  if (!success) logEvent(LogLevel::WARN, "HID report was not accepted");
  if (completed) {
    telemetryPublish("hid_command_executed", commandName(activeCommand.type));
    commandActive = false;
  }
}

static void statusRoute() {
  JsonDocument doc;
  doc["device"] = "ESP32-HID";
  doc["ble_keyboard_ready"] = ble.hidKeyboard().ready();
  doc["ble_mouse_ready"] = ble.hidMouse().ready();
  doc["wifi_mode"] = "AP+STA";
  doc["ap_ip"] = WiFi.softAPIP().toString();
  doc["sta_connected"] = WiFi.status() == WL_CONNECTED;
  doc["sta_ip"] = WiFi.localIP().toString();
  doc["queue_depth"] = uxQueueMessagesWaiting(commandQueue) + (commandActive ? 1 : 0);
  doc["queue_paused"] = queuePaused;
  doc["log_level"] = levelName(logLevel);
  doc["mode"] = modeName;
  doc["recording"] = recording;
  doc["ota_enabled"] = otaReady();
  doc["ota_busy"] = otaIsBusy();
  doc["ota_state"] = otaState();
  doc["firmware_version"] = otaCurrentVersion();
  sendJson(200, doc);
}

static bool queueRouteCommand(JsonVariantConst json) {
  Command command;
  if (!parseCommand(json, command)) return false;
  return enqueue(command);
}

static void commandRoute() {
  if (!requireAuth()) return;
  JsonDocument doc;
  if (deserializeJson(doc, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  Command command;
  if (!parseCommand(doc.as<JsonVariantConst>(), command)) { sendError(400, "invalid_command"); return; }
  if (!enqueue(command)) {
    sendError(recording && macroOverflow ? 409 : 429,
      recording && macroOverflow ? "macro_limit_exceeded" : "queue_full");
    return;
  }
  http.send(202, "application/json", "{\"queued\":true}");
}

static void sequenceRoute() {
  if (!requireAuth()) return;
  JsonDocument doc;
  if (deserializeJson(doc, http.arg("plain")) || !doc["commands"].is<JsonArray>()) {
    sendError(400, "invalid_sequence"); return;
  }
  JsonArray items = doc["commands"].as<JsonArray>();
  if (!items.size() || items.size() > config::MAX_MACRO_COMMANDS) {
    sendError(400, "invalid_sequence_size");
    return;
  }
  if (uxQueueSpacesAvailable(commandQueue) < items.size()) {
    sendError(429, "queue_full");
    return;
  }
  Command parsed[config::MAX_MACRO_COMMANDS];
  size_t count = 0;
  for (JsonVariant item : items) {
    if (!parseCommand(item.as<JsonVariantConst>(), parsed[count])) {
      sendError(400, "invalid_sequence_item"); return;
    }
    ++count;
  }
  if (!appendMacroBatch(parsed, count)) {
    sendError(409, "macro_limit_exceeded");
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    if (!enqueue(parsed[i], false)) { sendError(429, "queue_full"); return; }
  }
  JsonDocument response;
  response["queued"] = true;
  response["count"] = count;
  sendJson(202, response);
}

static void queueAction(const char* action) {
  if (!requireAuth()) return;
  if (!strcmp(action, "pause")) startPause();
  else if (!strcmp(action, "resume")) resumeQueue();
  else stopRequested = true;
  JsonDocument response;
  response["ok"] = true;
  response["action"] = action;
  sendJson(200, response);
}

static void mouseActionRoute(const char* action) {
  if (!requireAuth()) return;
  JsonDocument doc;
  if (deserializeJson(doc, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  if (!strcmp(action, "move")) doc["type"] = "mouse_move";
  else if (!strcmp(action, "click")) doc["type"] = "mouse_click";
  else doc["type"] = "mouse_wheel";
  Command command;
  if (!parseCommand(doc.as<JsonVariantConst>(), command)) { sendError(400, "invalid_mouse_command"); return; }
  if (!enqueue(command)) {
    sendError(recording && macroOverflow ? 409 : 429,
      recording && macroOverflow ? "macro_limit_exceeded" : "queue_full");
    return;
  }
  http.send(202, "application/json", "{\"queued\":true}");
}

static void logConfigRoute() {
  if (!requireAuth()) return;
  JsonDocument doc;
  if (deserializeJson(doc, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  String value = doc["level"] | "";
  value.toUpperCase();
  if (value == "NONE") logLevel = LogLevel::NONE;
  else if (value == "ERROR") logLevel = LogLevel::ERROR;
  else if (value == "WARN") logLevel = LogLevel::WARN;
  else if (value == "INFO") logLevel = LogLevel::INFO;
  else if (value == "DEBUG") logLevel = LogLevel::DEBUG;
  else if (value == "TRACE") logLevel = LogLevel::TRACE;
  else { sendError(400, "invalid_level"); return; }
  prefs.putUChar("logLevel", static_cast<uint8_t>(logLevel));
  http.send(200, "application/json", "{\"ok\":true}");
}

static void modeRoute() {
  if (!requireAuth()) return;
  JsonDocument doc;
  if (deserializeJson(doc, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  String requested = doc["mode"] | "";
  requested.toUpperCase();
  const char* modes[] = {"NORMAL", "TYPING", "INSTANT", "SEQUENCE", "RANDOMIZED", "QUIET", "DIAGNOSTIC"};
  bool valid = false;
  for (const char* mode : modes) if (requested == mode) valid = true;
  if (!valid) { sendError(400, "invalid_mode"); return; }
  modeName = requested;
  if (modeName == "INSTANT") typingDelayMs = 0;
  else if (modeName == "TYPING") typingDelayMs = 60;
  else if (modeName == "RANDOMIZED") typingJitterMs = 50;
  else if (modeName == "NORMAL") { typingDelayMs = config::DEFAULT_KEY_INTERVAL_MS; typingJitterMs = 0; }
  else if (modeName == "QUIET") logLevel = LogLevel::WARN;
  else if (modeName == "DIAGNOSTIC") logLevel = LogLevel::DEBUG;
  prefs.putString("mode", modeName);
  prefs.putUInt("typeDelay", typingDelayMs);
  prefs.putUShort("jitter", typingJitterMs);
  prefs.putUChar("logLevel", static_cast<uint8_t>(logLevel));
  http.send(200, "application/json", "{\"ok\":true}");
}

static void typingConfigRoute() {
  if (!requireAuth()) return;
  JsonDocument doc;
  if (deserializeJson(doc, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  const uint32_t delayMs = doc["delay_ms"] | typingDelayMs;
  const uint16_t jitterMsValue = doc["jitter_ms"] | typingJitterMs;
  if (delayMs > 1000 || jitterMsValue > config::MAX_RANDOM_JITTER_MS) {
    sendError(400, "out_of_range"); return;
  }
  typingDelayMs = delayMs;
  typingJitterMs = jitterMsValue;
  prefs.putUInt("typeDelay", delayMs);
  prefs.putUShort("jitter", jitterMsValue);
  http.send(200, "application/json", "{\"ok\":true}");
}

static void recordingStartRoute() {
  if (!requireAuth()) return;
  JsonDocument doc;
  if (deserializeJson(doc, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  if (!recordStart(doc["name"] | "")) { sendError(409, "invalid_name_or_already_recording"); return; }
  http.send(200, "application/json", "{\"recording\":true}");
}

static void recordingStopRoute() {
  if (!requireAuth()) return;
  const bool saved = recordStop();
  JsonDocument response;
  response["recording"] = false;
  response["saved"] = saved;
  response["commands"] = macroCount;
  if (macroOverflow) response["warning"] = "macro_too_large";
  sendJson(saved ? 200 : 409, response);
}

static void recordingStatusRoute() {
  if (!requireAuth()) return;
  JsonDocument doc;
  doc["recording"] = recording;
  doc["name"] = macroName;
  doc["commands"] = macroCount;
  doc["overflow"] = macroOverflow;
  sendJson(200, doc);
}

static String macroNames() {
  return prefs.getString("macroNames", "");
}

static void macrosRoute() {
  if (!requireAuth()) return;
  JsonDocument doc;
  JsonArray list = doc["macros"].to<JsonArray>();
  const String names = macroNames();
  int start = 0;
  while (start < static_cast<int>(names.length())) {
    int end = names.indexOf('|', start);
    String name = names.substring(start, end < 0 ? names.length() : end);
    if (name.length()) list.add(name);
    if (end < 0) break;
    start = end + 1;
  }
  sendJson(200, doc);
}

static void macroPlayRoute() {
  if (!requireAuth()) return;
  JsonDocument request;
  if (deserializeJson(request, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  String name = request["name"] | "";
  if (!safeMacroName(name)) { sendError(400, "invalid_macro_name"); return; }
  const String saved = prefs.getString(name.c_str(), "");
  if (!saved.length()) { sendError(404, "macro_not_found"); return; }
  JsonDocument macro;
  if (deserializeJson(macro, saved) || !macro["commands"].is<JsonArray>()) { sendError(500, "stored_macro_invalid"); return; }
  JsonArray items = macro["commands"].as<JsonArray>();
  if (!items.size() || items.size() > config::MAX_MACRO_COMMANDS) {
    sendError(500, "stored_macro_invalid");
    return;
  }
  if (uxQueueSpacesAvailable(commandQueue) < items.size()) {
    sendError(429, "queue_full");
    return;
  }
  Command commands[config::MAX_MACRO_COMMANDS];
  size_t count = 0;
  for (JsonVariant item : items) {
    if (!parseCommand(item.as<JsonVariantConst>(), commands[count])) {
      sendError(500, "stored_macro_invalid"); return;
    }
    ++count;
  }
  replayingMacro = true;
  for (size_t i = 0; i < count; ++i) enqueue(commands[i], false);
  replayingMacro = false;
  JsonDocument response;
  response["queued"] = true;
  response["count"] = count;
  sendJson(202, response);
}

static void macroDeleteRoute() {
  if (!requireAuth()) return;
  JsonDocument request;
  if (deserializeJson(request, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  String name = request["name"] | "";
  if (!safeMacroName(name)) { sendError(400, "invalid_macro_name"); return; }
  prefs.remove(name.c_str());
  const String names = macroNames();
  String updated = "";
  int start = 0;
  while (start < static_cast<int>(names.length())) {
    int end = names.indexOf('|', start);
    String token = names.substring(start, end < 0 ? names.length() : end);
    if (token.length() && token != name) {
      if (updated.length()) updated += "|";
      updated += token;
    }
    if (end < 0) break;
    start = end + 1;
  }
  prefs.putString("macroNames", updated);
  http.send(200, "application/json", "{\"deleted\":true}");
}

static void wifiConfigRoute() {
  if (!requireAuth()) return;
  JsonDocument doc;
  if (deserializeJson(doc, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  String ssid = doc["ssid"] | "";
  String password = doc["password"] | "";
  if (ssid.length() > 32 || password.length() > 63 || (ssid.length() && password.length() < 8)) {
    sendError(400, "invalid_wifi_credentials"); return;
  }
  staSsid = ssid;
  staPassword = password;
  prefs.putString("staSsid", staSsid);
  prefs.putString("staPass", staPassword);
  WiFi.disconnect(false, false);
  if (staSsid.length()) WiFi.begin(staSsid.c_str(), staPassword.c_str());
  http.send(200, "application/json", "{\"ok\":true,\"mode\":\"AP+STA\"}");
}

static void telemetryConfigRoute() {
  if (!requireAuth()) return;
  JsonDocument doc;
  if (deserializeJson(doc, http.arg("plain"))) { sendError(400, "invalid_json"); return; }
  const String endpoint = doc["endpoint"] | "";
  const String key = doc["api_key"] | "";
  const bool enabled = doc["enabled"] | false;
  if (!telemetryConfigure(endpoint, key, enabled)) {
    sendError(400, "https_endpoint_key_or_ca_required"); return;
  }
  http.send(200, "application/json", "{\"ok\":true}");
}

static void logsRoute() {
  if (!requireAuth()) return;
  JsonDocument doc;
  JsonArray list = doc["logs"].to<JsonArray>();
  const size_t start = (logWriteIndex + config::LOG_RING_CAPACITY - logCount) % config::LOG_RING_CAPACITY;
  for (size_t i = 0; i < logCount; ++i) list.add(logRing[(start + i) % config::LOG_RING_CAPACITY]);
  sendJson(200, doc);
}

static void websocketEvent(uint8_t id, WStype_t type, uint8_t* payload, size_t length) {
  if (id >= 32) return;
  if (type == WStype_CONNECTED) {
    websocketAuthenticated[id] = false;
    websocket.sendTXT(id, "AUTH_REQUIRED");
  } else if (type == WStype_DISCONNECTED) {
    websocketAuthenticated[id] = false;
  } else if (type == WStype_TEXT) {
    const bool valid = length == strlen(HID_API_SECRET)
      && memcmp(payload, HID_API_SECRET, length) == 0;
    if (valid) {
      websocketAuthenticated[id] = true;
      websocket.sendTXT(id, "AUTH_OK");
    } else {
      websocketAuthenticated[id] = false;
      websocket.sendTXT(id, "AUTH_FAILED");
    }
  }
}

static void otaStatusRoute() {
  if (!requireAuth(true)) return;
  JsonDocument doc;
  doc["enabled"] = otaReady();
  doc["busy"] = otaIsBusy();
  doc["state"] = otaState();
  doc["version"] = otaCurrentVersion();
  sendJson(200, doc);
}

static void otaStartRoute() {
  if (!requireAuth()) return;
  const size_t queued = uxQueueMessagesWaiting(commandQueue) + (commandActive ? 1 : 0);
  if (queued != 0 || queuePaused || recording || replayingMacro) {
    sendError(409, "queue_not_idle");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, http.arg("plain"))) {
    sendError(400, "invalid_json");
    return;
  }
  const String manifestUrl = String(doc["manifest_url"] | "");
  if (!otaReady()) {
    sendError(503, "ota_not_configured");
    return;
  }
  ble.hidKeyboard().releaseAll();
  ble.hidMouse().releaseAll();
  if (!otaStartUpdate(manifestUrl)) {
    sendError(503, "ota_start_failed");
    return;
  }
  http.send(202, "application/json", "{\"started\":true,\"state\":\"starting\"}");
}

static void setupRoutes() {
  const char* headers[] = {"Authorization"};
  http.collectHeaders(headers, 1);
  http.on("/api/status", HTTP_GET, statusRoute);
  http.on("/api/queue", HTTP_GET, []() {
    if (!requireAuth()) return;
    JsonDocument doc;
    doc["depth"] = uxQueueMessagesWaiting(commandQueue) + (commandActive ? 1 : 0);
    doc["active"] = commandActive;
    doc["paused"] = queuePaused;
    sendJson(200, doc);
  });
  http.on("/api/keyboard/type", HTTP_POST, commandRoute);
  http.on("/api/keyboard/press", HTTP_POST, commandRoute);
  http.on("/api/keyboard/hotkey", HTTP_POST, commandRoute);
  http.on("/api/keyboard/sleep", HTTP_POST, commandRoute);
  http.on("/api/keyboard/sequence", HTTP_POST, sequenceRoute);
  http.on("/api/mouse/move", HTTP_POST, [](){ mouseActionRoute("move"); });
  http.on("/api/mouse/click", HTTP_POST, [](){ mouseActionRoute("click"); });
  http.on("/api/mouse/wheel", HTTP_POST, [](){ mouseActionRoute("wheel"); });
  http.on("/api/queue/pause", HTTP_POST, [](){ queueAction("pause"); });
  http.on("/api/queue/resume", HTTP_POST, [](){ queueAction("resume"); });
  http.on("/api/queue/stop", HTTP_POST, [](){ queueAction("stop"); });
  http.on("/api/mode", HTTP_POST, modeRoute);
  http.on("/api/keyboard/config", HTTP_POST, typingConfigRoute);
  http.on("/api/logs", HTTP_GET, logsRoute);
  http.on("/api/logs/config", HTTP_POST, logConfigRoute);
  http.on("/api/recording/start", HTTP_POST, recordingStartRoute);
  http.on("/api/recording/stop", HTTP_POST, recordingStopRoute);
  http.on("/api/recording/status", HTTP_GET, recordingStatusRoute);
  http.on("/api/macros", HTTP_GET, macrosRoute);
  http.on("/api/macros/play", HTTP_POST, macroPlayRoute);
  http.on("/api/macros/delete", HTTP_POST, macroDeleteRoute);
  http.on("/api/wifi/config", HTTP_POST, wifiConfigRoute);
  http.on("/api/telemetry/config", HTTP_POST, telemetryConfigRoute);
  http.on("/api/ota/status", HTTP_GET, otaStatusRoute);
  http.on("/api/ota/update", HTTP_POST, otaStartRoute);
  http.on("/api/bluetooth/status", HTTP_GET, []() {
    JsonDocument doc;
    doc["keyboard_ready"] = ble.hidKeyboard().ready();
    doc["mouse_ready"] = ble.hidMouse().ready();
    doc["role"] = "BLE composite HID peripheral";
    doc["pairing"] = "Pair and reconnect from the host operating system Bluetooth settings";
    sendJson(200, doc);
  });
  http.on("/api/bluetooth/reconnect", HTTP_POST, []() {
    if (!requireAuth()) return;
    ble.advertising().start();
    http.send(200, "application/json", "{\"advertising\":true,\"host_controls_reconnect\":true}");
  });
  http.onNotFound([](){ sendError(404, "not_found"); });
}

static void serialCommand(const String& line) {
  if (otaIsBusy() && line != "status") {
    Serial.println("ERR ota in progress");
    return;
  }
  if (line == "help") {
    Serial.println("status | type <text> | key <key> | hotkey <CTRL+SHIFT+ESC>");
    Serial.println("mouse move <dx> <dy> | mouse click <left|right|middle> | mouse wheel <delta>");
    Serial.println("pause | resume | stop | advertise");
    Serial.println("record start <name> | record stop | macro list | macro play <name> | macro delete <name>");
    return;
  }
  if (line == "status") {
    Serial.printf("keyboard=%s mouse=%s queue=%u paused=%s\n",
      ble.hidKeyboard().ready() ? "ready" : "not-ready",
      ble.hidMouse().ready() ? "ready" : "not-ready",
      static_cast<unsigned>(uxQueueMessagesWaiting(commandQueue) + (commandActive ? 1 : 0)),
      queuePaused ? "yes" : "no");
    return;
  }
  if (line == "pause") { startPause(); return; }
  if (line == "resume") { resumeQueue(); return; }
  if (line == "stop") { stopRequested = true; return; }
  if (line == "advertise") {
    ble.advertising().start();
    Serial.println("OK advertising restarted; host controls reconnect");
    return;
  }
  if (line.startsWith("macro delete ")) {
    const String name = line.substring(13);
    if (!safeMacroName(name)) { Serial.println("ERR invalid macro name"); return; }
    prefs.remove(name.c_str());
    const String names = prefs.getString("macroNames", "");
    String updated;
    int start = 0;
    while (start < static_cast<int>(names.length())) {
      const int end = names.indexOf('|', start);
      const String item = names.substring(start, end < 0 ? names.length() : end);
      if (item.length() && item != name) {
        if (updated.length()) updated += "|";
        updated += item;
      }
      if (end < 0) break;
      start = end + 1;
    }
    prefs.putString("macroNames", updated);
    Serial.println("OK macro deleted");
    return;
  }
  if (line.startsWith("record start ")) {
    if (!recordStart(line.substring(13))) Serial.println("ERR invalid name or recording active");
    return;
  }
  if (line == "record stop") {
    if (!recordStop()) Serial.println("ERR recording empty, overflowed or not active");
    return;
  }

  JsonDocument json;
  if (line.startsWith("type ")) {
    json["type"] = "type"; json["value"] = line.substring(5);
  } else if (line.startsWith("key ")) {
    json["type"] = "press"; json["value"] = line.substring(4);
  } else if (line.startsWith("hotkey ")) {
    json["type"] = "hotkey"; json["value"] = line.substring(7);
  } else if (line.startsWith("mouse move ")) {
    const int split = line.indexOf(' ', 11);
    if (split < 0) { Serial.println("ERR usage: mouse move <dx> <dy>"); return; }
    json["type"] = "mouse_move";
    json["dx"] = line.substring(11, split).toInt();
    json["dy"] = line.substring(split + 1).toInt();
  } else if (line.startsWith("mouse click ")) {
    json["type"] = "mouse_click"; json["button"] = line.substring(12);
  } else if (line.startsWith("mouse wheel ")) {
    json["type"] = "mouse_wheel"; json["delta"] = line.substring(12).toInt();
  } else if (line == "macro list") {
    Serial.println(macroNames());
    return;
  } else if (line.startsWith("macro play ")) {
    const String name = line.substring(11);
    if (!safeMacroName(name)) { Serial.println("ERR invalid macro name"); return; }
    const String saved = prefs.getString(name.c_str(), "");
    JsonDocument macro;
    if (deserializeJson(macro, saved) || !macro["commands"].is<JsonArray>()) { Serial.println("ERR macro not found"); return; }
    JsonArray items = macro["commands"].as<JsonArray>();
    if (uxQueueSpacesAvailable(commandQueue) < items.size()) { Serial.println("ERR queue full"); return; }
    Command commands[config::MAX_MACRO_COMMANDS]; size_t count = 0;
    for (JsonVariant item : items) {
      if (count >= config::MAX_MACRO_COMMANDS || !parseCommand(item.as<JsonVariantConst>(), commands[count])) {
        Serial.println("ERR invalid macro"); return;
      }
      ++count;
    }
    for (size_t i = 0; i < count; ++i) enqueue(commands[i], false);
    return;
  } else {
    Serial.println("ERR unknown command");
    return;
  }

  Command command;
  if (!parseCommand(json.as<JsonVariantConst>(), command) || !enqueue(command)) {
    Serial.println("ERR invalid command or queue full");
  }
}

static void serialCli() {
  static String line;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      line.trim();
      if (line.length()) serialCommand(line);
      line = "";
    } else if (line.length() < 180) line += c;
  }
}

void setup() {
  Serial.begin(config::SERIAL_BAUD);
  delay(150);
  randomSeed(esp_random());

  prefs.begin("esp32hid", false);
  logLevel = static_cast<LogLevel>(prefs.getUChar("logLevel", static_cast<uint8_t>(LogLevel::INFO)));
  modeName = prefs.getString("mode", "NORMAL");
  typingDelayMs = prefs.getUInt("typeDelay", config::DEFAULT_KEY_INTERVAL_MS);
  typingJitterMs = prefs.getUShort("jitter", 0);
  staSsid = prefs.getString("staSsid", HID_STA_SSID);
  staPassword = prefs.getString("staPass", HID_STA_PASSWORD);
  commandQueue = xQueueCreate(config::COMMAND_QUEUE_CAPACITY, sizeof(Command));

  ble.hidKeyboard().configure();
  ble.hidMouse().configure();
  EspBleConfig bleConfig;
  bleConfig.deviceName = "ESP32-HID";
  bleConfig.security.enabled = true;
  bleConfig.security.bonding = true;
  if (!ble.begin(bleConfig)) {
    Serial.printf("BLE initialization failed: %s\n", ble.lastErrorDetail().c_str());
  } else {
    bleInitialized = true;
    ble.onDisconnected([](const EspBleConnection&) { ble.advertising().start(); });
    ble.advertising().setName(bleConfig.deviceName);
    ble.advertising().start();
  }

  WiFi.mode(WIFI_AP_STA);
  apStarted = WiFi.softAP(HID_AP_SSID, HID_AP_PASSWORD);
  if (staSsid.length()) WiFi.begin(staSsid.c_str(), staPassword.c_str());

  telemetryBegin();
  setupRoutes();
  http.begin();
  websocket.begin();
  websocket.onEvent(websocketEvent);
  otaBegin();
  otaConfirmPendingBoot(bleInitialized && apStarted && commandQueue != nullptr
    && WiFi.softAPIP() != IPAddress(0, 0, 0, 0));
  logEvent(LogLevel::INFO, String("Access point available at ") + WiFi.softAPIP().toString());
}

void loop() {
  ble.update();
  http.handleClient();
  websocket.loop();
  serialCli();
  processQueue();

  if (staSsid.length() && WiFi.status() != WL_CONNECTED && millis() - lastReconnectAt > 15000) {
    lastReconnectAt = millis();
    WiFi.reconnect();
  }
  delay(1);
}
