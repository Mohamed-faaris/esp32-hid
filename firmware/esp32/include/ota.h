#pragma once

#include <Arduino.h>

// OTA is disabled unless both an HTTPS trust anchor and an ECDSA public key
// are supplied at firmware build time.
void otaBegin();
bool otaReady();
bool otaIsBusy();
String otaState();
String otaCurrentVersion();
bool otaStartUpdate(const String& manifestUrl);

// Called once during setup after BLE, Wi-Fi, queue and HTTP startup.
void otaConfirmPendingBoot(bool startupHealthy);
