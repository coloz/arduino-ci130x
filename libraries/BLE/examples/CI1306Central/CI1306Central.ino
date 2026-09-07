// Use a second CI1306Peripheral board, or a peer exposing the same UUIDs.
#include <BLE.h>

const char serviceUuid[] = "19B10000-E8F2-537E-4F6C-D104768A1214";
const char counterUuid[] = "19B10001-E8F2-537E-4F6C-D104768A1214";
BLEDevice peer;
BLECharacteristic remoteCounter;
bool active = false;

void setup() {
  Serial.begin(115200);
  if (!BLE.begin()) return;
  BLE.setTimeout(3000);
  active = BLE.scanForUuid(serviceUuid);
}

void loop() {
  if (!active) { delay(10); return; }
  BLE.poll();
  if (!BLEESPLink.healthy()) {
    Serial.println("Controller session lost");
    Serial.println(BLEESPLink.lastError());
    remoteCounter = BLECharacteristic();
    peer = BLEDevice();
    BLE.end();
    active = false;
    return;
  }
  if (peer && !peer.connected()) {
    remoteCounter = BLECharacteristic();
    peer = BLEDevice();
    BLE.scanForUuid(serviceUuid);
  }
  if (!peer) {
    BLEDevice found = BLE.available();
    if (found) {
      BLE.stopScan();
      if (found.connect() && found.discoverAttributes()) {
        BLECharacteristic candidate = found.characteristic(counterUuid);
        if (candidate && candidate.canSubscribe() && candidate.subscribe()) {
          peer = found;
          remoteCounter = candidate;
          Serial.println("Counter subscribed");
        } else {
          found.disconnect();
        }
      } else if (found.connected()) {
        found.disconnect();
      }
      if (!peer) BLE.scanForUuid(serviceUuid);
    }
  } else if (remoteCounter.valueUpdated()) {
    uint32_t counter = 0;
    if (remoteCounter.readValue(counter) == sizeof(counter)) Serial.println(counter);
  }
  delay(1);
}
