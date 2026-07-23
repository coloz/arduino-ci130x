// CI1303 + SSD1306 128x64 I2C OLED text test.
//
// Wiring (CI1303):
//   OLED SDA -> PA2
//   OLED SCL -> PA3
//   OLED VCC -> the voltage required by the module
//   OLED GND -> GND
//
// Install "U8g2 by oliver" before compiling.

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kI2cClock = 100000;
constexpr uint8_t kSsd1306AddressA = 0x3C;
constexpr uint8_t kSsd1306AddressB = 0x3D;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
    U8G2_R0, U8X8_PIN_NONE);

uint8_t oledAddress = 0;
uint32_t frameCounter = 0;

bool deviceResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void drawScreen() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(18, 12, "CI1303 OLED");
  u8g2.drawHLine(0, 16, 128);

  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(5, 35, "Hello, U8g2!");

  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.setCursor(0, 52);
  u8g2.print("I2C: 0x");
  u8g2.print(oledAddress, HEX);
  u8g2.print("  Count: ");
  u8g2.print(frameCounter);

  // A moving pixel makes it obvious that the sketch is still running.
  u8g2.drawPixel(frameCounter % 128, 63);
  u8g2.sendBuffer();
}
}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  Serial.println();
  Serial.println("CI1303 U8g2 SSD1306 test starting");
  Serial.println("I2C pins: SDA=PA2, SCL=PA3");

  if (!Wire.begin()) {
    Serial.println("ERROR: Wire.begin() failed");
    while (true) {
      delay(1000);
    }
  }

  if (!Wire.setClock(kI2cClock)) {
    Serial.println("ERROR: Wire.setClock() failed");
    while (true) {
      delay(1000);
    }
  }

  if (deviceResponds(kSsd1306AddressA)) {
    oledAddress = kSsd1306AddressA;
  } else if (deviceResponds(kSsd1306AddressB)) {
    oledAddress = kSsd1306AddressB;
  } else {
    Serial.println("ERROR: no SSD1306 found at 0x3C or 0x3D");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("SSD1306 found at 0x");
  Serial.println(oledAddress, HEX);

  // U8g2 uses an 8-bit I2C address here, hence the left shift.
  u8g2.setI2CAddress(oledAddress << 1);
  u8g2.setBusClock(kI2cClock);
  u8g2.begin();
  drawScreen();
  Serial.println("OLED initialized; text test is running");
}

void loop() {
  ++frameCounter;
  drawScreen();

  Serial.print("OLED frame ");
  Serial.println(frameCounter);
  delay(1000);
}
