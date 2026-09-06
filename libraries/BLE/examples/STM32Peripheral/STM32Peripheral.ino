// ESPLink TX/RX/GND <-> ESP32-C3 running wifi_c3. No CTS/RTS is required.
// Peripheral service compatible with the ESPLinkCentral example.
#include <ESPLink.h>
#include <BLE.h>
#include <BLEESPLink.h>

// STM32duino example: NUCLEO-F411RE, USART1 RX=PA10 / TX=PA9.
// Choose another available pin pair when adapting to another STM32 board.
#if __has_include(<Serial.h>)
Uart LinkSerial(PA10, PA9); // STM32duino 3.x
#else
HardwareSerial LinkSerial(PA10, PA9); // STM32duino 2.x
#endif
#define LINK_UART LinkSerial

BLEService counterService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEUnsignedIntCharacteristic counter("19B10001-E8F2-537E-4F6C-D104768A1214",
                                     BLERead | BLENotify);
BLEByteCharacteristic command("19B10002-E8F2-537E-4F6C-D104768A1214",
                               BLERead | BLEWrite);
bool active = false;
uint32_t nextUpdate = 0;
unsigned int value = 0;

void setup() {
  Serial.begin(115200);
  LINK_UART.begin(115200);
  if (!ESPLink.begin(static_cast<Stream&>(LINK_UART)) || !BLE.begin()) {
    Serial.println("BLE controller initialization failed");
    Serial.println(BLEESPLink.lastError());
    return;
  }
  BLE.setLocalName("ESPLink-Counter");
  BLE.setAdvertisedService(counterService);
  counterService.addCharacteristic(counter);
  counterService.addCharacteristic(command);
  BLE.addService(counterService);
  counter.writeValue(value);
  command.writeValue(0);
  active = BLE.advertise();
}

void loop() {
  if (!active) { delay(10); return; }
  BLE.poll(); // Service the Host frequently; callbacks run in this task.
  if (!BLEESPLink.healthy()) {
    Serial.println("Controller session lost; restart BLE after restoring ESPLink");
    Serial.println(BLEESPLink.lastError());
    BLE.end();
    active = false;
    return;
  }
  if (command.written()) {
    if (command.value() == 1) value = 0;
    Serial.println("Command received");
  }
  if (uint32_t(millis() - nextUpdate) >= 1000) {
    nextUpdate = millis();
    counter.writeValue(++value); // Notify only subscribed peers.
  }
  delay(1);
}
