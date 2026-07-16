#include <Wire.h>

// Replace these values with the 7-bit address, writable register, and value
// for your I2C device. Connect external pull-ups to SDA pin 15/PB7 and
// SCL pin 16/PC0. Wire cannot be used together with Serial1.
constexpr uint8_t kDeviceAddress = 0x40;
constexpr uint8_t kRegister = 0x01;

void setup() {
  Serial.begin(921600);
  if (!Wire.begin()) {
    Serial.println("I2C setup failed");
    while (true) {
      delay(1000);
    }
  }
  Wire.setClock(100000);
}

void loop() {
  static uint8_t value = 0;

  Wire.beginTransmission(kDeviceAddress);
  Wire.write(kRegister);
  Wire.write(value);
  const uint8_t status = Wire.endTransmission();

  Serial.print("I2C write status: ");
  Serial.println(status);
  ++value;
  delay(1000);
}
