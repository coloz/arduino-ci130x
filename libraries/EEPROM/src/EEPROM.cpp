#include "EEPROM.h"

#include <Arduino.h>
#include <stdlib.h>

extern "C" {
#include "ci_nvdata_manage.h"

// ci_flash_data_info_init() initializes NVDM from the firmware partition
// table. Arduino setup() may begin while the higher-priority SDK init task is
// still running, so EEPROM.begin() waits for this readiness flag.
void is_ci_flash_data_info_inited(bool *state);
}

namespace {
constexpr uint32_t kInitTimeoutMs = 10000;
}

EEPROMClass EEPROM;

EEPROMClass::EEPROMClass() : _data(nullptr), _size(0), _dirty(false) {}

EEPROMClass::~EEPROMClass() {
  end();
}

bool EEPROMClass::begin(size_t size) {
  if (size == 0 || size > kMaxSize) {
    return false;
  }
  if (_data != nullptr && _size == size) {
    return true;
  }
  if (_data != nullptr) {
    end();
  }

  bool flashReady = false;
  uint32_t started = millis();
  do {
    is_ci_flash_data_info_inited(&flashReady);
    if (!flashReady) {
      delay(1);
    }
  } while (!flashReady && (millis() - started) < kInitTimeoutMs);
  if (!flashReady) {
    return false;
  }

  _data = static_cast<uint8_t *>(malloc(size));
  if (_data == nullptr) {
    return false;
  }
  _size = size;
  _dirty = false;
  memset(_data, 0xff, _size);

  cinv_item_ret_t initialized =
      cinv_item_init(kNvItemId, static_cast<uint16_t>(_size), _data);
  if (initialized == CINV_OPER_FAILED || initialized == CINV_ITEM_LEN_ERR) {
    free(_data);
    _data = nullptr;
    _size = 0;
    return false;
  }

  uint16_t actual = 0;
  cinv_item_ret_t readResult = cinv_item_read(
      kNvItemId, static_cast<uint16_t>(_size), _data, &actual);
  if (readResult != CINV_OPER_SUCCESS && readResult != CINV_ITEM_UNINIT) {
    free(_data);
    _data = nullptr;
    _size = 0;
    return false;
  }
  if (actual < _size) {
    memset(_data + actual, 0xff, _size - actual);
  }
  return true;
}

void EEPROMClass::end() {
  if (_data == nullptr) {
    return;
  }
  commit();
  free(_data);
  _data = nullptr;
  _size = 0;
  _dirty = false;
}

bool EEPROMClass::commit() {
  if (_data == nullptr) {
    return false;
  }
  if (!_dirty) {
    return true;
  }
  cinv_item_ret_t result = cinv_item_write(
      kNvItemId, static_cast<uint16_t>(_size), _data);
  if (result != CINV_OPER_SUCCESS) {
    return false;
  }
  _dirty = false;
  return true;
}

bool EEPROMClass::validRange(int address, size_t size) const {
  if (_data == nullptr || address < 0) {
    return false;
  }
  size_t start = static_cast<size_t>(address);
  return start <= _size && size <= (_size - start);
}

uint8_t EEPROMClass::read(int address) const {
  return validRange(address, 1) ? _data[address] : 0xff;
}

void EEPROMClass::write(int address, uint8_t value) {
  if (!validRange(address, 1)) {
    return;
  }
  _data[address] = value;
  _dirty = true;
}

void EEPROMClass::update(int address, uint8_t value) {
  if (read(address) != value) {
    write(address, value);
  }
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

