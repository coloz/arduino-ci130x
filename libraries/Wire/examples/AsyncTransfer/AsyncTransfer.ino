#include <Wire.h>

constexpr uint8_t kAddress = 0x40;
uint8_t requestRegister = 0x00;
uint8_t response[2];
volatile bool requestPending;

void transferDone(uint8_t status, size_t transferred, void *) {
  requestPending = false;
  Serial.print("Wire status: ");
  Serial.print(status);
  Serial.print(", bytes: ");
  Serial.println(transferred);
  if (status == 0 && transferred == sizeof(response)) {
    Serial.print("value: 0x");
    Serial.println((static_cast<uint16_t>(response[0]) << 8) | response[1],
                   HEX);
  }
}

void setup() {
  Serial.begin(115200);
  if (!Wire.begin()) {
    Serial.println("IIC0 is busy");
    return;
  }
  Wire.setWireTimeout(25000, true);
}

void loop() {
  if (!requestPending) {
    requestPending = Wire.transferAsync(
        kAddress, &requestRegister, 1, response, sizeof(response), true,
        transferDone);
    if (!requestPending) {
      Serial.print("start failed: ");
      Serial.println(Wire.lastError());
    }
  }
  delay(1000);
}
