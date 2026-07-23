#include <Wire.h>

void setup() {
  Serial.begin(115200);
  if (!Wire.begin()) {
    Serial.println("I2C setup failed");
    while (true) {
      delay(1000);
    }
  }
  Wire.setClock(100000);

  Serial.println("Scanning 7-bit I2C addresses...");
  uint8_t found = 0;
  for (uint8_t address = 1; address < 0x7f; ++address) {
    if (Wire.probe(address)) {
      Serial.print("Found device at 0x");
      if (address < 0x10) Serial.print('0');
      Serial.println(address, HEX);
      ++found;
    } else if (Wire.lastError() != 2) {
      Serial.print("Probe failed at 0x");
      Serial.println(address, HEX);
      break;
    }
  }
  Serial.print("Devices found: ");
  Serial.println(found);
}

void loop() {}
