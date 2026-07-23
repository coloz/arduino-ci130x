#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Keep the public values identical to Arduino-ESP32 Preferences. The private
// on-flash type codes remain compatible with records written by version 1.0.
enum PreferenceType : uint8_t {
  PT_I8 = 0,
  PT_U8,
  PT_I16,
  PT_U16,
  PT_I32,
  PT_U32,
  PT_I64,
  PT_U64,
  PT_STR,
  PT_BLOB,
  PT_INVALID,
};

class Preferences {
 public:
  static constexpr size_t kMaxNameLength = 15;
  static constexpr size_t kMaxKeyLength = 15;
  static constexpr size_t kMaxItemSize = 240;
  static constexpr size_t kMaxEntries = 16;

  Preferences();
  ~Preferences();

  Preferences(const Preferences &) = delete;
  Preferences &operator=(const Preferences &) = delete;

  bool begin(const char *name, bool readOnly = false,
             const char *partitionLabel = nullptr);
  void end();

  bool clear();
  bool remove(const char *key);
  bool isKey(const char *key);
  PreferenceType getType(const char *key);
  size_t freeEntries();
  bool isReadOnly() const;
  bool isBegun() const;

  size_t putChar(const char *key, int8_t value);
  size_t putUChar(const char *key, uint8_t value);
  size_t putShort(const char *key, int16_t value);
  size_t putUShort(const char *key, uint16_t value);
  size_t putInt(const char *key, int32_t value);
  size_t putUInt(const char *key, uint32_t value);
  size_t putLong(const char *key, int32_t value);
  size_t putULong(const char *key, uint32_t value);
  size_t putLong64(const char *key, int64_t value);
  size_t putULong64(const char *key, uint64_t value);
  size_t putFloat(const char *key, float value);
  size_t putDouble(const char *key, double value);
  size_t putBool(const char *key, bool value);
  size_t putString(const char *key, const char *value);
  size_t putString(const char *key, const String &value);
  size_t putBytes(const char *key, const void *value, size_t length);

  int8_t getChar(const char *key, int8_t defaultValue = 0);
  uint8_t getUChar(const char *key, uint8_t defaultValue = 0);
  int16_t getShort(const char *key, int16_t defaultValue = 0);
  uint16_t getUShort(const char *key, uint16_t defaultValue = 0);
  int32_t getInt(const char *key, int32_t defaultValue = 0);
  uint32_t getUInt(const char *key, uint32_t defaultValue = 0);
  int32_t getLong(const char *key, int32_t defaultValue = 0);
  uint32_t getULong(const char *key, uint32_t defaultValue = 0);
  int64_t getLong64(const char *key, int64_t defaultValue = 0);
  uint64_t getULong64(const char *key, uint64_t defaultValue = 0);
  float getFloat(const char *key, float defaultValue = NAN);
  double getDouble(const char *key, double defaultValue = NAN);
  bool getBool(const char *key, bool defaultValue = false);
  size_t getString(const char *key, char *value, size_t maxLength);
  String getString(const char *key, const String &defaultValue = String());
  size_t getStringLength(const char *key);
  size_t getBytesLength(const char *key);
  size_t getBytes(const char *key, void *value, size_t maxLength);

 private:
  struct NamespaceHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t nameLength;
    uint16_t used;
    char name[kMaxNameLength + 1];
  };

  struct EntryHeader {
    uint8_t keyLength;
    uint8_t type;
    uint16_t valueLength;
  };

  struct EntryView {
    size_t offset;
    size_t totalLength;
    EntryHeader header;
    const uint8_t *value;
  };

  enum class StoredType : uint8_t {
    U8 = 0x01,
    U16 = 0x02,
    U32 = 0x04,
    U64 = 0x08,
    I8 = 0x11,
    I16 = 0x12,
    I32 = 0x14,
    I64 = 0x18,
    String = 0x21,
    Blob = 0x42,
  };

  static_assert(sizeof(NamespaceHeader) == 24,
                "Preferences namespace format changed");
  static_assert(offsetof(NamespaceHeader, magic) == 0,
                "Preferences namespace magic offset changed");
  static_assert(offsetof(NamespaceHeader, version) == 4,
                "Preferences namespace version offset changed");
  static_assert(offsetof(NamespaceHeader, nameLength) == 5,
                "Preferences namespace name-length offset changed");
  static_assert(offsetof(NamespaceHeader, used) == 6,
                "Preferences namespace used-length offset changed");
  static_assert(offsetof(NamespaceHeader, name) == 8,
                "Preferences namespace name offset changed");
  static_assert(sizeof(EntryHeader) == 4,
                "Preferences entry format changed");
  static_assert(offsetof(EntryHeader, keyLength) == 0,
                "Preferences entry key-length offset changed");
  static_assert(offsetof(EntryHeader, type) == 1,
                "Preferences entry type offset changed");
  static_assert(offsetof(EntryHeader, valueLength) == 2,
                "Preferences entry value-length offset changed");

  static constexpr uint32_t kMagic = 0x46455250UL;  // "PREF"
  static constexpr uint8_t kFormatVersion = 1;
  static constexpr uint32_t kIdBase = 0xE0000000UL;
  static constexpr uint32_t kIdMask = 0x0FFFFFFFUL;
  static constexpr uint8_t kProbeCount = 16;

  bool waitForNvData() const;
  void resetState();
  bool load();
  bool store();
  bool initializeNamespace(const char *name, uint32_t id);
  bool validNamespace(const char *name, size_t length) const;
  bool validKey(const char *key, size_t *length = nullptr) const;
  bool findEntry(const char *key, EntryView *entry);
  bool entryAt(size_t offset, EntryView *entry) const;
  size_t entryCount() const;
  size_t putValue(const char *key, StoredType type, const void *value,
                   size_t length);
  bool getValue(const char *key, StoredType type, void *value,
                 size_t length);
  bool getBlobUnlocked(const char *key, StoredType type,
                       const uint8_t **value, size_t *length);
  static bool validStoredValue(uint8_t type, const uint8_t *value,
                               size_t length);
  static PreferenceType publicType(uint8_t storedType);
  static bool namespaceIdMatches(const char *name, uint32_t id);
  static uint32_t namespaceHash(const char *name);
  static uint32_t candidateId(uint32_t hash, uint8_t attempt);

  alignas(4) uint8_t _data[kMaxItemSize];
  char _name[kMaxNameLength + 1];
  uint16_t _length;
  uint32_t _id;
  bool _begun;
  bool _readOnly;
};
