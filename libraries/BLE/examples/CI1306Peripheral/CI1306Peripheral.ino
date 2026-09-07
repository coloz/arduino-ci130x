// CI1306 TX/RX/GND <-> ESP32-C3 running wifi_c3. No CTS/RTS is required.
// Peripheral service compatible with the CI1306Central example.
#include <BLE.h>

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
  if (!BLE.begin()) {
    Serial.println("BLE controller initialization failed");
    Serial.println(BLEESPLink.lastError());
    return;
  }
  BLE.setLocalName("CI1306-Counter");
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
