#include <Arduino.h>

// UART0 console: TX=pin 13/PB5, RX=pin 14/PB6, 921600 baud.
// UART1 device:  TX=pin 15/PB7, RX=pin 16/PC0, 115200 baud.
// UART1 shares both pads with Wire and cannot be used at the same time. The
// current board profile configures UART1 TX as open-drain, so add an external
// pull-up and use 3.3 V-compatible levels with a common ground.
void setup() {
  Serial.begin(921600);
  Serial1.begin(115200);
  Serial.println("UART0 <-> UART1 bridge started");
}

void loop() {
  while (Serial.available() > 0) {
    Serial1.write(static_cast<uint8_t>(Serial.read()));
  }
  while (Serial1.available() > 0) {
    Serial.write(static_cast<uint8_t>(Serial1.read()));
  }
}
