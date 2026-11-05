#include <Arduino.h>
#include "config.h"

void setup() {
  Serial.begin(config::SERIAL_BAUD);
  delay(100);
  Serial.println();
  Serial.println("ESP32 HID Controller scaffold");
  Serial.println("Firmware modules are not implemented yet.");
}

void loop() {
  delay(1000);
}
