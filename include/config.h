#pragma once

#include <Arduino.h>

namespace config {
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr size_t LOG_RING_CAPACITY = 128;
constexpr size_t COMMAND_QUEUE_CAPACITY = 24;
constexpr size_t MAX_TEXT_LENGTH = 256;
constexpr uint32_t DEFAULT_KEY_INTERVAL_MS = 35;
constexpr uint32_t MAX_RANDOM_JITTER_MS = 500;
}
