#include <Arduino.h>

void setup() {
  Serial.begin(921600);
  analogReadResolution(12);
}

void loop() {
  Serial.println(analogRead(A0));
  delay(250);
}
