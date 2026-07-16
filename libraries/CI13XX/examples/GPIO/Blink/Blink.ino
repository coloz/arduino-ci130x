#include <CI13XX.h>

// These boards have no Arduino-controlled built-in LED.
#if defined(CI_CHIP_CI1302) || defined(CI_CHIP_CI1303)
// Connect an external LED and series resistor from pin 20 (PC4) to GND.
constexpr uint8_t kLedPin = 20;
#else
// Connect an external LED and series resistor from pin 11 (PB3) to GND.
constexpr uint8_t kLedPin = 11;
#endif

void setup() {
  pinMode(kLedPin, OUTPUT);
}

void loop() {
  digitalWrite(kLedPin, HIGH);
  delay(500);
  digitalWrite(kLedPin, LOW);
  delay(500);
}
