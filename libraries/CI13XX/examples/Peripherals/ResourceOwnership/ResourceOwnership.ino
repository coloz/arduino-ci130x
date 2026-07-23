#include <Arduino.h>
#include <PeripheralManager.h>
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // Wire and Serial1 share the same pads on every supported variant.
  Serial1.begin(115200);
  if (!Serial1) {
    const PeripheralConflict conflict = PeripheralManager.lastConflict();
    Serial.print("Serial1 blocked by ");
    Serial.println(PeripheralManager.ownerName(conflict.currentOwner));
  }

  Wire.end();
  Serial1.begin(115200);
  Serial.println(Serial1 ? "Serial1 acquired after Wire.end()"
                         : "Serial1 still unavailable");
}

void loop() {}
