#include <CI13XX.h>

// A0 is PC4/AIN2. Keep the input within the electrical limits documented for
// CI1306 and share GND with the signal source.
void setup() {
  Serial.begin(921600);
  analogReadResolution(12);
}

void loop() {
  Serial.println(analogRead(A0));
  delay(250);
}
