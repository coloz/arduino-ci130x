#include <Wire.h>

// CI130X as an I2C peripheral. A controller can write bytes to address 0x42
// and read back the most recently received byte.
volatile uint8_t lastValue = 0;

void receiveEvent(int count) {
  while (count-- > 0 && Wire.available()) {
    lastValue = static_cast<uint8_t>(Wire.read());
  }
}

void requestEvent() {
  Wire.write(lastValue);
}

void setup() {
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
  Wire.begin(0x42);
}

void loop() {
  delay(100);
}
