#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <ChipIntelliASR.h>

#if defined(CI_CHIP_CI1302) || defined(CI_CHIP_CI1303)
static constexpr uint8_t kLedPin = 20;  // PC4; connect an external LED.
#else
static constexpr uint8_t kLedPin = 11;  // PB3; connect an external LED.
#endif
static const char kBanner[] PROGMEM = "CI13XX Arduino smoke test";

void setup() {
  Serial.begin(921600);
  if (pgm_read_byte(kBanner) == 'C') {
    Serial.println(FPSTR(kBanner));
  }
  String stringCheck("String");
  stringCheck += stringCheck.c_str();  // Exercises alias-safe buffer growth.
  stringCheck += ' ';
  stringCheck += 2.7f;                 // Does not depend on newlib printf-float.
  Serial.println(stringCheck);         // Expected: StringString 2.70

  pinMode(kLedPin, OUTPUT);
  Wire.begin();
  EEPROM.begin(32);
  ChipIntelliASR.begin();
}

void loop() {
  static unsigned long changedAt = 0;
  if (millis() - changedAt >= 500) {
    changedAt = millis();
    digitalToggle(kLedPin);
  }

  ChipIntelliASRClass::Result result;
  if (ChipIntelliASR.read(result)) {
    Serial.print("ASR cmd=");
    Serial.print(result.commandId);
    Serial.print(" semantic=");
    Serial.print(result.semanticId);
    Serial.print(" score=");
    Serial.print(result.score);
    Serial.print(" text=");
    Serial.println(result.text);
  }
}
