#include <CI13XX.h>

// CI-D06GT01D has no Arduino-controlled built-in LED. Connect an external LED
// and series resistor from Arduino pin 11 (PB3) to GND.
constexpr uint8_t kLedPin = 11;

void setup() {
  pinMode(kLedPin, OUTPUT);
}

void loop() {
  digitalWrite(kLedPin, HIGH);
  delay(500);
  digitalWrite(kLedPin, LOW);
  delay(500);
}
