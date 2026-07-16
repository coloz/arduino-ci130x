#include <CI13XX.h>

#if defined(CI_CHIP_CI1302) || defined(CI_CHIP_CI1303)
// Connect a push button between Arduino pin 20 (PC4) and GND.
constexpr uint8_t kButtonPin = 20;
#else
// Connect a push button between Arduino pin 12 (PB4) and GND.
constexpr uint8_t kButtonPin = 12;
#endif

void setup() {
  Serial.begin(921600);
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
