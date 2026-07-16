#include <CI13XX.h>

// Connect a push button between Arduino pin 12 (PB4) and GND. PA/PB/PC GPIOs
// support interrupts; PD0-PD5 do not.
constexpr uint8_t kButtonPin = 12;
volatile bool gButtonPressed = false;
volatile uint32_t gPressCount = 0;

void onButtonPressed() {
  // Keep the ISR short. Printing is done by loop(), not from interrupt context.
  gButtonPressed = true;
  ++gPressCount;
}

void setup() {
  Serial.begin(921600);
  pinMode(kButtonPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kButtonPin), onButtonPressed, FALLING);
  Serial.println("Press the button on pin 12");
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
