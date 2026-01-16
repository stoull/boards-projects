#include <Arduino.h>
#include "StorageUsage.h"

void setup() {
    Serial.begin(115200);
    Serial.printf("is runing");
}

void loop() {
  delay(1000);
  test_storage_usage();
  test_SPIFFS_Usage();
}