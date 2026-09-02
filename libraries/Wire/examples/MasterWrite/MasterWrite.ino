#include <Wire.h>

// Replace these values with the 7-bit address, writable register, and value
// for your I2C device. Most boards require external pull-ups on SDA/SCL;
// easyVoice 1306 dev already has 4.7 kOhm pull-ups on PB3/PB4.
constexpr uint8_t kDeviceAddress = 0x40;
constexpr uint8_t kRegister = 0x01;

void setup() {
  Serial.begin(115200);
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
