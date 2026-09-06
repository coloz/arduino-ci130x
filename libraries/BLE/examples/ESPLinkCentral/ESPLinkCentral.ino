// Use a second ESPLinkPeripheral board, or a peer exposing the same UUIDs.
#include <ESPLink.h>
#include <BLE.h>
#include <BLEESPLink.h>

// Select the hardware UART wired to C3 RX/TX/GND. Change Serial1 for your board.
// UART pins, baud and RX buffering remain the application/core responsibility.
#define LINK_UART Serial1

const char serviceUuid[] = "19B10000-E8F2-537E-4F6C-D104768A1214";
const char counterUuid[] = "19B10001-E8F2-537E-4F6C-D104768A1214";
BLEDevice peer;
BLECharacteristic remoteCounter;
bool active = false;

void setup() {
  Serial.begin(115200);
  LINK_UART.begin(115200);
  if (!ESPLink.begin(static_cast<Stream&>(LINK_UART)) || !BLE.begin()) return;
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
