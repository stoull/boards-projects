#include "DeviceInfoHelper.h"

void setup() {
  Serial.begin(115200);
  delay(2000);
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.println("On looping ... ");
  publish_device_info_data();
  delay(60000);
}