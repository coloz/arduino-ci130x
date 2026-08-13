// CI1306 + SSD1306 128x64 hardware-I2C OLED test.
//
// Wiring:
//   OLED SDA -> PB7
//   OLED SCL -> PC0
//   OLED VCC -> the voltage required by the OLED module
//   OLED GND -> GND
//
// External I2C pull-up resistors are recommended.

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
bool oledReady = false;

void drawTestScreen();

uint8_t u8g2I2cAddress() {
  return u8x8_GetI2CAddress(u8g2.getU8x8()) >> 1;
}

void printAddress(uint8_t address) {
  Serial.print("0x");
  if (address < 0x10) Serial.print('0');
  Serial.print(address, HEX);
}

uint8_t sendSsd1306Command(uint8_t command) {
  Wire.beginTransmission(oledAddress);
  Wire.write(0x00);  // SSD1306 command control byte.
  Wire.write(command);
  const uint8_t status = Wire.endTransmission();
  Serial.print("SSD1306 command 0x");
  if (command < 0x10) Serial.print('0');
  Serial.print(command, HEX);
  Serial.print(" status=");
  Serial.println(status);
  return status;
}

bool runRawDisplayTest() {
  Serial.println("Raw SSD1306 test: display ON, then all pixels ON for 3 s");
  if (sendSsd1306Command(0xAF) != 0) return false;  // Display ON.
  if (sendSsd1306Command(0xA5) != 0) return false;  // Ignore RAM, all ON.
  delay(3000);
  return sendSsd1306Command(0xA4) == 0;             // Resume RAM display.
}

bool deviceResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool startWire() {
  Wire.end();
  // Wire.begin() performs an ownership-safe bus recovery. Manipulating SDA
  // and SCL through pinMode() here would claim them as GPIO and prevent Wire
  // from acquiring the I2C peripheral afterwards.
  if (!Wire.begin()) {
    Serial.println("ERROR: Wire.begin() failed");
    return false;
  }
  if (!Wire.setClock(kI2cClock)) {
    Serial.println("ERROR: Wire.setClock() failed");
    return false;
  }
  return true;
}

bool scanForOled() {
  bool anyDevice = false;
  oledAddress = 0;

  Serial.println("Scanning I2C addresses...");
  for (uint8_t address = 1; address < 0x7f; ++address) {
    if (!deviceResponds(address)) {
      continue;
    }

    anyDevice = true;
    Serial.print("I2C device found at 0x");
    if (address < 0x10) Serial.print('0');
    Serial.println(address, HEX);
    if (address == kSsd1306AddressA || address == kSsd1306AddressB) {
      oledAddress = address;
    }
  }

  if (!anyDevice) {
    Serial.println("No I2C device responded");
  } else if (oledAddress == 0) {
    Serial.println("No OLED found at 0x3C or 0x3D");
  }
  return oledAddress != 0;
}

void initializeOled() {
  Serial.print("Initializing OLED at 0x");
  Serial.println(oledAddress, HEX);
  if (!runRawDisplayTest()) {
    Serial.print("Raw SSD1306 command failed; Wire.lastError()=");
    Serial.println(Wire.lastError());
    return;
  }
  u8g2.setI2CAddress(oledAddress << 1);
  u8g2.setBusClock(kI2cClock);
  Serial.print("U8g2 address before begin: ");
  printAddress(u8g2I2cAddress());
  Serial.println();
  u8g2.begin();
  const uint8_t beginError = Wire.lastError();
  Serial.print("Wire status after u8g2.begin(): error=");
  Serial.print(beginError);
  Serial.print(", timeout=");
  Serial.println(Wire.getWireTimeoutFlag() ? "yes" : "no");
  Serial.print("U8g2 address after begin: ");
  printAddress(u8g2I2cAddress());
  Serial.println();
  Serial.print("Probe expected address immediately after begin: ");
  Serial.println(deviceResponds(oledAddress) ? "ACK" : "NACK");
  oledReady = true;
  drawTestScreen();
  const uint8_t firstFrameError = Wire.lastError();
  Serial.print("Wire status after first sendBuffer(): error=");
  Serial.print(firstFrameError);
  Serial.print(", timeout=");
  Serial.println(Wire.getWireTimeoutFlag() ? "yes" : "no");
  Serial.print("Probe expected address after first sendBuffer: ");
  Serial.println(deviceResponds(oledAddress) ? "ACK" : "NACK");
  sendSsd1306Command(0xAF);
  Serial.println("OLED initialized; dynamic test is running");
}

void drawTestScreen() {
  u8g2.clearBuffer();

  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(39, 12, "CI1306");
  u8g2.drawHLine(1, 16, 126);

  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(17, 34, "U8G2 TEST OK");

  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.setCursor(5, 46);
  u8g2.print("I2C 0x");
  u8g2.print(oledAddress, HEX);
  u8g2.print("  PB7/PC0");

  // The moving bar and counter confirm that repeated I2C updates work.
  const uint8_t progress = frameCounter % 116U;
  u8g2.drawFrame(5, 51, 118, 7);
  u8g2.drawBox(6, 52, progress, 5);
  u8g2.setCursor(98, 63);
  u8g2.print(frameCounter);

  u8g2.sendBuffer();
}
}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  Serial.println();
  Serial.println("CI1306 U8g2 SSD1306 test starting");
  Serial.println("I2C pins: SDA=PB7 (15), SCL=PC0 (16)");

  if (startWire() && scanForOled()) {
    initializeOled();
  }
}

void loop() {
  if (!oledReady) {
    Serial.println("Retrying I2C setup and scan...");
    if (startWire() && scanForOled()) {
      initializeOled();
    }
    delay(2000);
    return;
  }

  ++frameCounter;
  drawTestScreen();
  const uint8_t frameError = Wire.lastError();

  if (frameCounter % 20U == 0) {
    Serial.print("Frame ");
    Serial.print(frameCounter);
    Serial.print(" error=");
    Serial.print(frameError);
    Serial.print(" expected=");
    printAddress(oledAddress);
    Serial.print(" u8g2=");
    printAddress(u8g2I2cAddress());
    Serial.print(" probe=");
    Serial.print(deviceResponds(oledAddress) ? "ACK" : "NACK");
    Serial.print(" timeout=");
    Serial.println(Wire.getWireTimeoutFlag() ? "yes" : "no");
  }
  delay(100);
}
