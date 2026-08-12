#include <Wire.h>

// Replace these values with the 7-bit address and register of your sensor.
constexpr uint8_t kDeviceAddress = 0x40;
constexpr uint8_t kRegister = 0x00;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(100000);
}

void loop() {
  Wire.beginTransmission(kDeviceAddress);
  Wire.write(kRegister);

  // false sends the write now without STOP, so requestFrom() uses a
  // repeated START while this controller still owns the bus.
  if (Wire.endTransmission(false) != 0) {
    Serial.println("I2C write setup failed");
    delay(1000);
    return;
  }

  if (Wire.requestFrom(kDeviceAddress, 2) == 2) {
    uint16_t value = static_cast<uint16_t>(Wire.read()) << 8;
    value |= static_cast<uint8_t>(Wire.read());
    Serial.println(value);
  } else {
    Serial.println("I2C read failed");
  }
  delay(1000);
}
