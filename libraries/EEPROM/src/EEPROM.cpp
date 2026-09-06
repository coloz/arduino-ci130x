#include "EEPROM.h"

#include <Arduino.h>
#include <stdlib.h>

extern "C" {
#include "ci130x_core_misc.h"
#include "ci_nvdata_manage.h"

// NVDM is initialized from the firmware partition table by the SDK init task.
void is_ci_flash_data_info_inited(bool *state);
}

namespace {
constexpr uint32_t kInitTimeoutMs = 10000;
}

EEPROMClass EEPROM;

EEPROMClass::EEPROMClass()
    : _data(nullptr), _size(0), _storageSize(0), _dirty(false) {}

EEPROMClass::~EEPROMClass() {
  // Destruction cannot report failure or retain a retryable buffer. Call
  // commit()/end() explicitly and check the result when persistence matters.
  if (_data != nullptr) {
    commit();
    free(_data);
  }
}

bool EEPROMClass::begin(size_t size) {
  if (check_curr_trap() != 0 || size == 0 || size > kMaxSize) {
    return false;
  }
  if (_data != nullptr) {
    // A logical resize never discards uncommitted changes or the hidden tail.
    _size = size;
    if (size > _storageSize) {
      _storageSize = size;
    }
    return true;
  }
  if (!chipintelli_sdk_begin()) {
    return false;
  }

  bool flashReady = false;
  const uint32_t started = millis();
  do {
    is_ci_flash_data_info_inited(&flashReady);
    if (!flashReady) {
      if (chipintelli_sdk_state() == CHIPINTELLI_SDK_FAILED) {
        return false;
      }
      delay(1);
    }
  } while (!flashReady && static_cast<uint32_t>(millis() - started) < kInitTimeoutMs);
  if (!flashReady) {
    return false;
  }

  // The SDK reads only the requested length but verifies the checksum over
  // the entire stored item. Always read the maximum payload, even when the
  // caller exposes a smaller range or an older firmware saved a shorter item.
  uint8_t *data = static_cast<uint8_t *>(malloc(kMaxSize));
  if (data == nullptr) {
    return false;
  }
  memset(data, 0xff, kMaxSize);

  const cinv_item_ret_t initialized =
      cinv_item_init(kNvItemId, static_cast<uint16_t>(size), data);
  if (initialized != CINV_OPER_SUCCESS && initialized != CINV_ITEM_UNINIT) {
    free(data);
    return false;
  }

  uint16_t actual = 0;
  const cinv_item_ret_t readResult = cinv_item_read(
      kNvItemId, static_cast<uint16_t>(kMaxSize), data, &actual);
  // UNINIT means the item is missing here, not that a read succeeded.
  if (readResult != CINV_OPER_SUCCESS || actual == 0 || actual > kMaxSize) {
    free(data);
    return false;
  }
  if (actual < kMaxSize) {
    memset(data + actual, 0xff, kMaxSize - actual);
  }

  _data = data;
  _size = size;
  _storageSize = actual > size ? actual : size;
  _dirty = false;
  return true;
}

bool EEPROMClass::end() {
  if (_data == nullptr) {
    return true;
  }
  if (!commit()) {
    return false;
  }
  free(_data);
  _data = nullptr;
  _size = 0;
  _storageSize = 0;
  _dirty = false;
  return true;
}

bool EEPROMClass::commit() {
  if (check_curr_trap() != 0 || _data == nullptr) {
    return false;
  }
  if (!_dirty) {
    return true;
  }
  // Persist the hidden tail too, so shrinking the accessible range cannot
  // truncate data. Keep short records short to avoid needless Flash wear.
  const cinv_item_ret_t result = cinv_item_write(
      kNvItemId, static_cast<uint16_t>(_storageSize), _data);
  if (result != CINV_OPER_SUCCESS) {
    return false;
  }
  // The SDK does not propagate every internal item-status write failure.
  // Verify the visible record before reporting success or dropping dirty data.
  uint8_t verified[kMaxSize];
  uint16_t actual = 0;
  if (cinv_item_read(kNvItemId, static_cast<uint16_t>(kMaxSize), verified,
                     &actual) != CINV_OPER_SUCCESS ||
      actual != _storageSize || memcmp(verified, _data, _storageSize) != 0) {
    return false;
  }
  _dirty = false;
  return true;
}

bool EEPROMClass::validRange(int address, size_t size) const {
  if (_data == nullptr || address < 0) {
    return false;
  }
  const size_t start = static_cast<size_t>(address);
  return start <= _size && size <= (_size - start);
}

uint8_t EEPROMClass::read(int address) const {
  return validRange(address, 1) ? _data[address] : 0xff;
}

void EEPROMClass::write(int address, uint8_t value) {
  if (!validRange(address, 1) || _data[address] == value) {
    return;
  }
  _data[address] = value;
  _dirty = true;
}

void EEPROMClass::update(int address, uint8_t value) {
  write(address, value);
}

size_t EEPROMClass::readBytes(int address, void *value, size_t size) const {
  if (value == nullptr || !validRange(address, size)) {
    return 0;
  }
  memcpy(value, _data + address, size);
  return size;
}

size_t EEPROMClass::writeBytes(int address, const void *value, size_t size) {
  if (value == nullptr || !validRange(address, size)) {
    return 0;
  }
  if (memcmp(_data + address, value, size) != 0) {
    memcpy(_data + address, value, size);
    _dirty = true;
  }
  return size;
}

size_t EEPROMClass::length() const {
  return _size;
}

bool EEPROMClass::isBegun() const {
  return _data != nullptr;
}
