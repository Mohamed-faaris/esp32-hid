#pragma once
#include <Arduino.h>
namespace config {
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr size_t LOG_RING_CAPACITY = 48;
constexpr size_t COMMAND_QUEUE_CAPACITY = 24;
constexpr size_t MAX_TEXT_LENGTH = 96;
constexpr size_t MAX_MACRO_COMMANDS = 24;
constexpr uint32_t DEFAULT_KEY_INTERVAL_MS = 35;
constexpr uint16_t MAX_RANDOM_JITTER_MS = 250;
constexpr int8_t MAX_MOUSE_DELTA = 100;
constexpr char FIRMWARE_VERSION[] = "1.0.0";
constexpr uint32_t FIRMWARE_SECURITY_VERSION = 1;
}
