#include <Arduino.h>

#if defined(CI_BOARD_CI_D06GT01D)
// The development board has an active-high green LED on PD1.
constexpr uint8_t kLedPin = LED_BUILTIN;
#elif defined(CI_CHIP_CI1302) || defined(CI_CHIP_CI1303)
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
