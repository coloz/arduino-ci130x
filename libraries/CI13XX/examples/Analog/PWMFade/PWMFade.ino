#include <CI13XX.h>

#if defined(CI_CHIP_CI1302) || defined(CI_CHIP_CI1303)
// Connect an external LED and series resistor from pin 20 (PC4/PWM0) to GND.
constexpr uint8_t kLedPin = 20;
#else
// Connect an external LED and series resistor from pin 11 (PB3/PWM4) to GND.
constexpr uint8_t kLedPin = 11;
#endif

void setup() {
  analogWriteResolution(8);
  analogWriteFrequency(1000);
}

void loop() {
  static int value = 0;
  static int step = 5;

  analogWrite(kLedPin, value);
  if (value <= 0) {
    step = 5;
  } else if (value >= 255) {
    step = -5;
  }
  value += step;
  delay(20);
}
