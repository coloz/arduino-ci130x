// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>

// Isolate radio operations, not the production GATT/attribute ownership code.
// A lifecycle test must never perform an actual peer or advertising operation.
#define _BLE_DEVICE_H_
class BLEDevice {};
#define _BLE_LOCAL_DEVICE_H_
class LifecycleLocalDevice {
public:
  bool setAdvertisedServiceData(uint16_t, const uint8_t*, int) { std::abort(); }
  int advertise() { std::abort(); }
};
inline LifecycleLocalDevice BLE;
#define _ATT_H_
class ATTClass {
public:
  int handleInd(uint16_t, const uint8_t*, int) { std::abort(); }
  int handleNotify(uint16_t, const uint8_t*, int) { std::abort(); }
  bool connected() { std::abort(); }
  bool connected(uint16_t) { std::abort(); }
  uint16_t mtu(uint16_t) { std::abort(); }
  int writeReq(uint16_t, uint16_t, const uint8_t*, int, uint8_t*) { std::abort(); }
  void writeCmd(uint16_t, uint16_t, const uint8_t*, int) { std::abort(); }
  int readReq(uint16_t, uint16_t, uint8_t*) { std::abort(); }
};
inline ATTClass ATT;
#define _GAP_H_
class LifecycleGap {
public:
  bool advertising() { std::abort(); }
};
inline LifecycleGap GAP;
