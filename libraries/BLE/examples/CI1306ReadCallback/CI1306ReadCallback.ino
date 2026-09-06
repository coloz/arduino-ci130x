// ArduinoBLE Host runs on CI1306: this callback updates the value before the
// ATT Read Response is formed. Keep the callback short; no WiFi or blocking RPC.
#include <ESPLink.h>
#include <BLE.h>
#include <BLEESPLink.h>

BLEService uptimeService("7E400001-B5A3-F393-E0A9-E50E24DCCA9E");
BLEUnsignedLongCharacteristic uptime("7E400002-B5A3-F393-E0A9-E50E24DCCA9E", BLERead);
bool active = false;

void onRead(BLEDevice device, BLECharacteristic characteristic) {
  (void)device;
  const uint32_t value = millis();
  characteristic.writeValue(value);
}

void setup() {
  Serial.begin(115200);
  if (!ESPLink.begin(Serial2, 115200) || !BLE.begin()) return;
  BLE.setLocalName("CI1306-Uptime");
  BLE.setAdvertisedService(uptimeService);
  uptimeService.addCharacteristic(uptime);
  BLE.addService(uptimeService);
  uptime.writeValue(0);
  uptime.setEventHandler(BLERead, onRead);
  active = BLE.advertise();
}

void loop() {
  if (active) {
    BLE.poll();
    if (!BLEESPLink.healthy()) { BLE.end(); active = false; }
  }
  delay(1);
}
