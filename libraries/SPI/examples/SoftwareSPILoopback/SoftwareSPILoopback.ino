#include <SPI.h>

// Default CI1302/CI1303/CI1306 wiring for this self-test:
//   PA4 / Arduino 4 / MOSI ---- PA2 / Arduino 2 / MISO
// SCK (PA5 / 5) and SS (PA3 / 3) may be observed with a logic analyzer.
// PA4 is a boot strap input during reset, so only make this connection to a
// high-impedance input (as in this loopback) while the board is resetting.

constexpr uint32_t kSoftwareSpiClock = 100000;
const uint8_t kPatterns[] = {0x00, 0x55, 0xaa, 0xff, 0x3c, 0xc3};

void setup() {
  Serial.begin(921600);
  if (!SPI.begin()) {
    Serial.println("SPI pin setup failed");
    while (true) {
      delay(1000);
    }
  }
  Serial.println("Software SPI loopback: connect MOSI pin 4 to MISO pin 2");
}

void loop() {
  static size_t index = 0;
  const uint8_t sent = kPatterns[index];

  SPI.beginTransaction(SPISettings(kSoftwareSpiClock, MSBFIRST, SPI_MODE0));
  digitalWrite(SS, LOW);
  const uint8_t received = SPI.transfer(sent);
  digitalWrite(SS, HIGH);
  SPI.endTransaction();

  Serial.print("TX=0x");
  Serial.print(sent, HEX);
  Serial.print(" RX=0x");
  Serial.print(received, HEX);
  Serial.println(received == sent ? " PASS" : " FAIL");

  index = (index + 1) % (sizeof(kPatterns) / sizeof(kPatterns[0]));
  delay(500);
}
