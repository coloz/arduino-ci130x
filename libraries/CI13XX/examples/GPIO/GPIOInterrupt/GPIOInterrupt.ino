#include <Arduino.h>

#if defined(CI_CHIP_CI1302) || defined(CI_CHIP_CI1303)
// Connect a push button between PC4 and GND.
constexpr uint8_t kButtonPin = PC4;
#else
// Connect a push button between PB4 and GND. PA/PB/PC GPIOs
// support interrupts; PD0-PD5 do not.
constexpr uint8_t kButtonPin = PB4;
#endif
volatile bool gButtonPressed = false;
volatile uint32_t gPressCount = 0;

void onButtonPressed() {
  // attachInterrupt() defers this callback to the Arduino task. Keep it short
  // so other deferred events are not delayed; printing remains in loop().
  gButtonPressed = true;
  ++gPressCount;
}

void setup() {
  Serial.begin(115200);
  pinMode(kButtonPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kButtonPin), onButtonPressed, FALLING);
  Serial.print("Press the button on pin ");
  Serial.println(kButtonPin);
}

void loop() {
  if (gButtonPressed) {
    noInterrupts();
    const uint32_t count = gPressCount;
    gButtonPressed = false;
    interrupts();

    Serial.print("Interrupt count: ");
    Serial.println(count);
  }
  delay(10);
}
