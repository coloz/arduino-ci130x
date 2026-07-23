#include <Arduino.h>

void setup() {
  // The CI130X UART supports the standard Arduino data/parity/stop settings.
  Serial.begin(115200, SERIAL_8E1);
  if (!Serial) {
    return;
  }
  Serial.println("Buffered UART ready");
}

void loop() {
  while (Serial.available()) {
    Serial.write(static_cast<uint8_t>(Serial.read()));
  }

  static unsigned long reportedAt = 0;
  if (millis() - reportedAt >= 1000) {
    reportedAt = millis();
    const HardwareSerialErrorCounts errors = Serial.errorCounts();
    if (errors.bufferOverflow || errors.hardwareOverrun || errors.framing ||
        errors.parity || errors.breakCondition) {
      Serial.print("UART errors: overflow=");
      Serial.print(errors.bufferOverflow);
      Serial.print(" overrun=");
      Serial.print(errors.hardwareOverrun);
      Serial.print(" framing=");
      Serial.print(errors.framing);
      Serial.print(" parity=");
      Serial.println(errors.parity);
      Serial.clearErrorCounts();
    }
  }
}
