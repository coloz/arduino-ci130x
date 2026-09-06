#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class EEPROMClass {
public:
  static constexpr size_t kMaxSize = 240;

  EEPROMClass();
  ~EEPROMClass();

  EEPROMClass(const EEPROMClass &) = delete;
  EEPROMClass &operator=(const EEPROMClass &) = delete;

  // Size selects the accessible range; resizing preserves buffered bytes.
  bool begin(size_t size = 128);
  // A failed commit leaves the instance open so the caller can retry.
  bool end();
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
  // Maximum of the saved length and all ranges opened in this session.
  size_t _storageSize;
  bool _dirty;
};

extern EEPROMClass EEPROM;

