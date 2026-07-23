// Displays DHT20 temperature and humidity on a 128x64 SSD1306 OLED.
//
// Install these libraries with Arduino Library Manager before compiling:
//   - U8g2 by oliver
//   - DHT20 by Rob Tillaart
//
// Connect the OLED and DHT20 to the same I2C bus. The selected CI13XX board
// variant supplies SDA and SCL; external pull-up resistors are required.

#include <Arduino.h>
#include <DHT20.h>
#include <U8g2lib.h>
#include <Wire.h>

namespace {
constexpr uint32_t kI2cClock = 400000;
constexpr uint32_t kSampleIntervalMs = 1000;

// This constructor targets the common 0.96-inch, 128x64 SSD1306 I2C OLED.
// For an OLED at address 0x3D, call u8g2.setI2CAddress(0x3D * 2) before begin().
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
    U8G2_R0, U8X8_PIN_NONE);
DHT20 dht20(&Wire);

void drawMessage(const char *title, const char *message, int status) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 14, title);
  u8g2.drawHLine(0, 18, 128);
  u8g2.drawStr(0, 36, message);
  if (status != DHT20_OK) {
    u8g2.setCursor(0, 54);
    u8g2.print("Status: ");
    u8g2.print(status);
  }
  u8g2.sendBuffer();
}

void drawReading(float temperature, float humidity) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 12, "DHT20");
  u8g2.drawHLine(0, 16, 128);

  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.setCursor(0, 39);
  u8g2.print(temperature, 1);
  u8g2.print(" C");

  u8g2.setCursor(0, 62);
  u8g2.print(humidity, 1);
  u8g2.print(" %RH");
  u8g2.sendBuffer();
}
}  // namespace

void setup() {
  Serial.begin(115200);

  if (!Wire.begin()) {
    Serial.println("I2C initialization failed");
    while (true) {
      delay(1000);
    }
  }
  if (!Wire.setClock(kI2cClock)) {
    Serial.println("I2C clock configuration failed");
    while (true) {
      delay(1000);
    }
  }

  u8g2.setBusClock(kI2cClock);
  u8g2.begin();
  drawMessage("DHT20", "Initializing...", DHT20_OK);

  // DHT20::begin() uses an address-only endTransmission(). CI13XX Wire maps
  // that operation to its dedicated START/address/ACK/STOP probe transaction.

  (void)dht20.begin();

  // The DHT20 library requires at least one second before the first read and
  // between subsequent reads.
  delay(kSampleIntervalMs);
}

void loop() {
  const int status = dht20.read();
  if (status == DHT20_OK) {
    const float temperature = dht20.getTemperature();
    const float humidity = dht20.getHumidity();

    Serial.print("Temperature: ");
    Serial.print(temperature, 1);
    Serial.print(" C, Humidity: ");
    Serial.print(humidity, 1);
    Serial.println(" %RH");

    drawReading(temperature, humidity);
  } else {
    Serial.print("DHT20 read failed, status: ");
    Serial.println(status);
    drawMessage("DHT20", "Read failed", status);
  }

  delay(kSampleIntervalMs);
}
