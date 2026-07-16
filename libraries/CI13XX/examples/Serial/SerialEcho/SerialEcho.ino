#include <CI13XX.h>

// Serial is UART0 on PB5/TX (pin 13) and PB6/RX (pin 14). It is also the SDK
// log port; Serial.begin() reinitializes it at the requested supported baud.
void setup() {
  Serial.begin(921600);
  Serial.println("Type characters; UART0 will echo them");
}

void loop() {
  while (Serial.available() > 0) {
    Serial.write(static_cast<uint8_t>(Serial.read()));
  }
}
