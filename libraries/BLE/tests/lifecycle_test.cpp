// SPDX-License-Identifier: MIT
#include <iostream>
#include "BLEProperty.h"
#include "local/BLELocalCharacteristic.h"
#include "local/BLELocalDescriptor.h"
#include "local/BLELocalService.h"
#include "utility/GATT.h"

static unsigned checks = 0, deletedServices = 0, deletedCharacteristics = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::abort(); } } while (0)
class TrackedService : public BLELocalService {
public:
  TrackedService() : BLELocalService("1234") {}
  ~TrackedService() override { ++deletedServices; }
  using BLELocalService::characteristicCount;
  using BLELocalService::startHandle;
  using BLELocalService::endHandle;
};
class TrackedCharacteristic : public BLELocalCharacteristic {
public:
  TrackedCharacteristic() : BLELocalCharacteristic("5678", BLERead | BLEWrite | BLENotify, 20) {}
  ~TrackedCharacteristic() override { ++deletedCharacteristics; }
};
class ServiceHandle : public BLEService {
public:
  explicit ServiceHandle(TrackedService* value) : BLEService(value) {}
  TrackedService* get() { return static_cast<TrackedService*>(local()); }
};
class CharacteristicHandle : public BLECharacteristic {
public:
  explicit CharacteristicHandle(TrackedCharacteristic* value) : BLECharacteristic(value) {}
  TrackedCharacteristic* get() { return static_cast<TrackedCharacteristic*>(local()); }
};
class InspectableGATT : public GATTClass {
public:
  using GATTClass::attributeCount;
  using GATTClass::attribute;
};

static void testStandaloneClear() {
  const unsigned before = deletedCharacteristics;
  ServiceHandle service(new TrackedService);
  {
    CharacteristicHandle characteristic(new TrackedCharacteristic);
    service.addCharacteristic(characteristic);
    CHECK(service.get()->characteristicCount() == 1);
  }
  CHECK(deletedCharacteristics == before);
  service.clear();
  CHECK(deletedCharacteristics == before + 1);
  CHECK(service.get()->characteristicCount() == 0);
  service.clear();
  CHECK(deletedCharacteristics == before + 1);
}

static void testGATTOwnedService() {
  const unsigned servicesBefore = deletedServices, charsBefore = deletedCharacteristics;
  InspectableGATT gatt; gatt.begin();
  CHECK(gatt.attributeCount() == 9);
  {
    ServiceHandle service(new TrackedService);
    CharacteristicHandle characteristic(new TrackedCharacteristic);
    service.addCharacteristic(characteristic); gatt.addService(service);
    CHECK(gatt.attributeCount() == 13);
  } // GATT now owns the only service reference.
  CHECK(deletedServices == servicesBefore && deletedCharacteristics == charsBefore);
  gatt.end();
  CHECK(gatt.attributeCount() == 0);
  CHECK(deletedServices == servicesBefore + 1 && deletedCharacteristics == charsBefore + 1);
  gatt.end(); // Explicit shutdown followed by destruction must also be safe.
}

static void testRepeatedRegistration() {
  InspectableGATT gatt;
  ServiceHandle service(new TrackedService);
  CharacteristicHandle characteristic(new TrackedCharacteristic);
  for (unsigned cycle = 0; cycle < 100; ++cycle) {
    gatt.begin();
    CHECK(gatt.attributeCount() == 9);
    CHECK(service.get()->characteristicCount() == 0);
    service.addCharacteristic(characteristic); gatt.addService(service);
    CHECK(gatt.attributeCount() == 13);
    CHECK(gatt.attribute(9) == service.get());
    CHECK(gatt.attribute(10) == characteristic.get() && gatt.attribute(11) == characteristic.get());
    CHECK(gatt.attribute(12)->type() == BLETypeDescriptor);
    CHECK(std::strcmp(gatt.attribute(12)->uuid(), "2902") == 0);
    CHECK(service.get()->startHandle() == 10 && service.get()->endHandle() == 13);
    uint8_t payload[20];
    for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = uint8_t(cycle + i);
    CHECK(characteristic.writeValue(payload, sizeof(payload)) == int(sizeof(payload)));
    CHECK(characteristic.valueLength() == int(sizeof(payload)));
    CHECK(std::memcmp(characteristic.value(), payload, sizeof(payload)) == 0);
    // Public handle + service membership + declaration/value attribute entries.
    CHECK(characteristic.get()->retain() == 5);
    CHECK(characteristic.get()->release() == 4);
    gatt.end(); gatt.end();
    CHECK(gatt.attributeCount() == 0 && service.get()->characteristicCount() == 0);
    CHECK(service.get()->startHandle() == 0 && service.get()->endHandle() == 0);
    CHECK(characteristic.get()->retain() == 2); // only the public handle remains
    CHECK(characteristic.get()->release() == 1);
    CHECK(std::memcmp(characteristic.value(), payload, sizeof(payload)) == 0);
  }
}

static void testRetainedBuiltinAndRepeatedBegin() {
  InspectableGATT gatt;
  gatt.end(); gatt.end();
  gatt.begin();
  auto* retainedName = static_cast<BLELocalCharacteristic*>(gatt.attribute(1));
  retainedName->retain();
  gatt.end(); gatt.end();
  CHECK(retainedName->writeValue("retained") == 8);
  CHECK(retainedName->release() == 0);
  delete retainedName;
  for (unsigned i = 0; i < 20; ++i) {
    gatt.begin(); // includes begin without an intervening end
    CHECK(gatt.attributeCount() == 9);
    gatt.setDeviceName("restart"); gatt.setAppearance(0x1234);
    const auto* name = static_cast<BLELocalCharacteristic*>(gatt.attribute(1));
    CHECK(name->valueLength() == 7 && std::memcmp(name->value(), "restart", 7) == 0);
  }
}

int main() {
  testStandaloneClear(); testGATTOwnedService(); testRepeatedRegistration();
  testRetainedBuiltinAndRepeatedBegin();
  CHECK(deletedServices == 3 && deletedCharacteristics == 3);
  std::cout << "BLE production GATT lifecycle: " << checks << " assertions passed\n";
}
