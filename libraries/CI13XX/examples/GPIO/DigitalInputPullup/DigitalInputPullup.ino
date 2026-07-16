#include <CI13XX.h>

// Connect a push button between Arduino pin 12 (PB4) and GND.
constexpr uint8_t kButtonPin = 12;

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
