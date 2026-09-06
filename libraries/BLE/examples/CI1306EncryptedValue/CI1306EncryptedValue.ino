// LE encrypted attribute smoke-test. A phone should pair before accessing it.
// This example has no display/confirmation and DOES NOT provide MITM protection.
// Keys are not persisted. It is not a production authentication example.
// See README-CI13XX.md for the upstream security implementation's limits.
#include <ESPLink.h>
#include <BLE.h>
#include <BLEESPLink.h>

BLEService secureService("7E410001-B5A3-F393-E0A9-E50E24DCCA9E");
BLEUnsignedIntCharacteristic secureValue("7E410002-B5A3-F393-E0A9-E50E24DCCA9E",
                                          BLERead | BLEWrite | BLEEncryption);
bool active = false;

void setup() {
  Serial.begin(115200);
  if (!ESPLink.begin(Serial2, 115200) || !BLE.begin()) return;
  BLE.setPairable(YES);
  BLE.setLocalName("CI1306-Encrypted");
  BLE.setAdvertisedService(secureService);
  secureService.addCharacteristic(secureValue);
  BLE.addService(secureService);
  secureValue.writeValue(42);
  active = BLE.advertise();
}

void loop() {
  if (active) {
    BLE.poll();
    if (!BLEESPLink.healthy()) { BLE.end(); active = false; }
    else if (secureValue.written()) {
      Serial.print("Encrypted peer wrote: ");
      Serial.println(secureValue.value());
    }
  }
  delay(1);
}
