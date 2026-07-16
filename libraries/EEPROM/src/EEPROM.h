#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class EEPROMClass {
public:
  static constexpr size_t kMaxSize = 240;

  EEPROMClass();
  ~EEPROMClass();

  bool begin(size_t size = 128);
  void end();
  bool commit();

  uint8_t read(int address) const;
  void write(int address, uint8_t value);
  void update(int address, uint8_t value);

  size_t readBytes(int address, void *value, size_t size) const;
  size_t writeBytes(int address, const void *value, size_t size);

  template <typename T>
  T &get(int address, T &value) const {
    readBytes(address, &value, sizeof(T));
    return value;
  }

  template <typename T>
  const T &put(int address, const T &value) {
    writeBytes(address, &value, sizeof(T));
    return value;
  }

  size_t length() const;
  bool isBegun() const;

private:
  static constexpr uint32_t kNvItemId = 0x60454550UL;  // Arduino EEPROM

  bool validRange(int address, size_t size) const;

  uint8_t *_data;
  size_t _size;
  bool _dirty;
};

extern EEPROMClass EEPROM;

