#include <Arduino.h>

constexpr uint8_t kLedPin = PA4;
constexpr uint32_t kBlinkIntervalMs = 100;
constexpr uint8_t kLedHighMarker = '3';
constexpr uint8_t kLedLowMarker = 'n';

uint32_t nextToggleMs = 0;
bool ledHigh = false;

void setup() {
  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, LOW);

  Serial.begin(115200);
}

void loop() {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextToggleMs) >= 0) {
    nextToggleMs = now + kBlinkIntervalMs;
    ledHigh = !ledHigh;
    digitalWrite(kLedPin, ledHigh ? HIGH : LOW);

    // Keep GPIO timing independent from a broken or disconnected UART. A
    // single status byte is sent only when the hardware FIFO has room.
    if (Serial.availableForWrite() > 0) {
      // "3n3n..." uniquely identifies the CI1303 citool-cli flash test.
      Serial.write(ledHigh ? kLedHighMarker : kLedLowMarker);
    }
  }

  while (Serial.available() > 0 && Serial.availableForWrite() > 0) {
    const int received = Serial.read();
    if (received >= 0) {
      Serial.write(static_cast<uint8_t>(received));
    }
  }

  delay(1);
}
