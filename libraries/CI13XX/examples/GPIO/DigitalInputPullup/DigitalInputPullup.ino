#include <Arduino.h>

#if defined(CI_CHIP_CI1302) || defined(CI_CHIP_CI1303)
// Connect a push button between PC4 and GND.
constexpr uint8_t kButtonPin = PC4;
#else
// Connect a push button between PB4 and GND.
constexpr uint8_t kButtonPin = PB4;
#endif

void setup() {
  Serial.begin(115200);
  pinMode(kButtonPin, INPUT_PULLUP);
  Serial.println("Button: released");
}

void loop() {
  static int previous = HIGH;
  const int current = digitalRead(kButtonPin);
  if (current != previous) {
    Serial.println(current == LOW ? "Button: pressed" : "Button: released");
    previous = current;
  }
  delay(20);  // Simple switch debounce.
}
