#pragma once
#include <Arduino.h>

void telemetryBegin();
bool telemetryConfigure(const String& endpoint, const String& apiKey, bool enabled);
void telemetryPublish(const char* eventName, const char* detail);
