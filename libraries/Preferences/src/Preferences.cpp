#include "Preferences.h"

#include <math.h>
#include <string.h>

extern "C" {
#include "FreeRTOS.h"
#include "ci130x_core_misc.h"
#include "ci_nvdata_manage.h"
#include "semphr.h"
#include "task.h"

void is_ci_flash_data_info_inited(bool *state);
}

namespace {
constexpr uint32_t kInitTimeoutMs = 10000;

SemaphoreHandle_t s_preferencesMutex = nullptr;

SemaphoreHandle_t preferencesMutex() {
  if (check_curr_trap() != 0) {
    return nullptr;
  }

  taskENTER_CRITICAL();
  SemaphoreHandle_t mutex = s_preferencesMutex;
  taskEXIT_CRITICAL();
  if (mutex != nullptr) {
    return mutex;
  }

  SemaphoreHandle_t created = xSemaphoreCreateMutex();
  if (created == nullptr) {
    return nullptr;
  }

  taskENTER_CRITICAL();
  if (s_preferencesMutex == nullptr) {
    s_preferencesMutex = created;
    created = nullptr;
  }
  mutex = s_preferencesMutex;
  taskEXIT_CRITICAL();

  if (created != nullptr) {
    vSemaphoreDelete(created);
  }
  return mutex;
}

class PreferencesLock {
 public:
  PreferencesLock() : _mutex(preferencesMutex()), _locked(false) {
    if (_mutex != nullptr) {
      _locked = xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE;
    }
  }

  ~PreferencesLock() {
    if (_locked) {
      xSemaphoreGive(_mutex);
    }
  }

  bool locked() const { return _locked; }

 private:
  SemaphoreHandle_t _mutex;
  bool _locked;
};
}  // namespace

Preferences::Preferences()
    : _data{},
      _name{},
      _length(0),
      _id(0),
      _begun(false),
      _readOnly(false) {}

Preferences::~Preferences() {
  end();
}

bool Preferences::waitForNvData() const {
  bool ready = false;
  const uint32_t started = millis();
  do {
    is_ci_flash_data_info_inited(&ready);
    if (!ready) {
      if (chipintelli_sdk_state() == CHIPINTELLI_SDK_FAILED) {
        return false;
      }
      delay(1);
    }
  } while (!ready && (millis() - started) < kInitTimeoutMs);
  return ready;
}

uint32_t Preferences::namespaceHash(const char *name) {
  uint32_t hash = 2166136261UL;
  while (*name != '\0') {
    hash ^= static_cast<uint8_t>(*name++);
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t Preferences::candidateId(uint32_t hash, uint8_t attempt) {
  const uint32_t mixed = hash + static_cast<uint32_t>(attempt) * 0x09E3779BUL;
  return kIdBase | (mixed & kIdMask);
}

bool Preferences::namespaceIdMatches(const char *name, uint32_t id) {
  if (name == nullptr) {
    return false;
  }
  const uint32_t hash = namespaceHash(name);
  for (uint8_t attempt = 0; attempt < kProbeCount; ++attempt) {
    if (candidateId(hash, attempt) == id) {
      return true;
    }
  }
  return false;
}

bool Preferences::validNamespace(const char *name, size_t length) const {
  return name != nullptr && length > 0 && length <= kMaxNameLength;
}

bool Preferences::validKey(const char *key, size_t *length) const {
  if (key == nullptr) {
    return false;
  }
  const size_t keyLength = strnlen(key, kMaxKeyLength + 1);
  if (keyLength == 0 || keyLength > kMaxKeyLength) {
    return false;
  }
  if (length != nullptr) {
    *length = keyLength;
  }
  return true;
}

bool Preferences::initializeNamespace(const char *name, uint32_t id) {
  if (!namespaceIdMatches(name, id)) {
    return false;
  }
  memset(_data, 0, sizeof(_data));
  NamespaceHeader header{};
  const size_t nameLength = strlen(name);
  header.magic = kMagic;
  header.version = kFormatVersion;
  header.nameLength = static_cast<uint8_t>(nameLength);
  header.used = sizeof(NamespaceHeader);
  memcpy(header.name, name, nameLength);
  header.name[nameLength] = '\0';
  memcpy(_data, &header, sizeof(header));

  const cinv_item_ret_t result = cinv_item_init(
      id, static_cast<uint16_t>(sizeof(NamespaceHeader)), _data);
  if (result == CINV_OPER_SUCCESS) {
    // Another task created this ID between our read and init calls. Re-read
    // and accept it only when it is the exact namespace we intended to open.
    uint16_t actual = 0;
    if (cinv_item_read(id, static_cast<uint16_t>(sizeof(_data)), _data,
                       &actual) != CINV_OPER_SUCCESS ||
        actual < sizeof(NamespaceHeader)) {
      return false;
    }
    NamespaceHeader existing{};
    memcpy(&existing, _data, sizeof(existing));
    if (existing.magic != kMagic || existing.version != kFormatVersion ||
        existing.used != actual || existing.nameLength != nameLength ||
        existing.name[nameLength] != '\0' ||
        strnlen(existing.name, kMaxNameLength + 1) != nameLength ||
        !namespaceIdMatches(existing.name, id) ||
        memcmp(existing.name, name, nameLength) != 0) {
      return false;
    }
    _id = id;
    _length = actual;
    return true;
  }
  if (result != CINV_ITEM_UNINIT) {
    return false;
  }
  _id = id;
  _length = sizeof(NamespaceHeader);
  return true;
}

bool Preferences::begin(const char *name, bool readOnly,
                        const char *partitionLabel) {
  if (check_curr_trap() != 0) {
    return false;
  }
  if (name == nullptr) {
    return false;
  }
  const size_t nameLength = strnlen(name, kMaxNameLength + 1);
  if (!validNamespace(name, nameLength)) {
    return false;
  }
  if (partitionLabel != nullptr) {
    if (partitionLabel[0] == '\0' ||
        (strcmp(partitionLabel, "nvs") != 0 &&
         strcmp(partitionLabel, "ci-nvdm") != 0)) {
      return false;
    }
  }
  if (!chipintelli_sdk_begin() || !waitForNvData()) {
    return false;
  }

  PreferencesLock lock;
  if (!lock.locked() || _begun) {
    return false;
  }
  resetState();

  const uint32_t hash = namespaceHash(name);
  bool hasEmpty = false;
  uint32_t firstEmpty = 0;
  bool hasMatch = false;
  uint32_t matchingId = 0;
  for (uint8_t attempt = 0; attempt < kProbeCount; ++attempt) {
    const uint32_t id = candidateId(hash, attempt);
    uint16_t actual = 0;
    const cinv_item_ret_t result = cinv_item_read(
        id, static_cast<uint16_t>(sizeof(_data)), _data, &actual);
    if (result == CINV_ITEM_UNINIT) {
      if (!hasEmpty) {
        hasEmpty = true;
        firstEmpty = id;
      }
      continue;
    }
    if (result != CINV_OPER_SUCCESS) {
      // A checksum/read failure is not a hash collision. Do not create a
      // second copy of the same namespace at another probe position.
      return false;
    }
    if (actual < sizeof(NamespaceHeader)) {
      uint32_t magic = 0;
      if (actual >= sizeof(magic)) {
        memcpy(&magic, _data, sizeof(magic));
      }
      // A damaged Preferences header must not fork into another probe slot.
      // A shorter record with a different magic is simply an SDK/user item.
      if (magic == kMagic) {
        return false;
      }
      continue;
    }

    NamespaceHeader header{};
    memcpy(&header, _data, sizeof(header));
    if (header.magic != kMagic) {
      continue;
    }
    if (header.version != kFormatVersion ||
        header.nameLength == 0 || header.nameLength > kMaxNameLength ||
        header.used != actual || header.used > sizeof(_data) ||
        header.name[header.nameLength] != '\0' ||
        strnlen(header.name, kMaxNameLength + 1) != header.nameLength ||
        !namespaceIdMatches(header.name, id)) {
      return false;
    }
    if (header.nameLength != nameLength ||
        memcmp(header.name, name, nameLength) != 0) {
      continue;
    }
    if (hasMatch) {
      return false;
    }
    hasMatch = true;
    matchingId = id;
  }

  if (hasMatch) {
    _id = matchingId;
    _readOnly = readOnly;
    memcpy(_name, name, nameLength + 1);
    _begun = true;
    if (!load()) {
      resetState();
      return false;
    }
    return true;
  }

  if (readOnly || !hasEmpty || !initializeNamespace(name, firstEmpty)) {
    return false;
  }
  _readOnly = false;
  memcpy(_name, name, nameLength + 1);
  _begun = true;
  if (entryCount() > kMaxEntries) {
    resetState();
    return false;
  }
  return true;
}

void Preferences::resetState() {
  memset(_data, 0, sizeof(_data));
  memset(_name, 0, sizeof(_name));
  _length = 0;
  _id = 0;
  _begun = false;
  _readOnly = false;
}

void Preferences::end() {
  PreferencesLock lock;
  if (lock.locked()) {
    resetState();
  }
}

bool Preferences::load() {
  if (!_begun) {
    return false;
  }
  uint16_t actual = 0;
  const cinv_item_ret_t result = cinv_item_read(
      _id, static_cast<uint16_t>(sizeof(_data)), _data, &actual);
  if (result != CINV_OPER_SUCCESS || actual < sizeof(NamespaceHeader)) {
    return false;
  }
  NamespaceHeader header{};
  memcpy(&header, _data, sizeof(header));
  const size_t nameLength = strnlen(_name, kMaxNameLength + 1);
  if (header.magic != kMagic || header.version != kFormatVersion ||
      header.used != actual || header.used > sizeof(_data) ||
      header.nameLength == 0 || header.nameLength > kMaxNameLength ||
      header.name[header.nameLength] != '\0' ||
      strnlen(header.name, kMaxNameLength + 1) != header.nameLength ||
      !namespaceIdMatches(header.name, _id) ||
      header.nameLength != nameLength ||
      memcmp(header.name, _name, nameLength) != 0) {
    return false;
  }
  _length = actual;
  return entryCount() <= kMaxEntries;
}

bool Preferences::store() {
  if (!_begun || _readOnly || _length < sizeof(NamespaceHeader) ||
      _length > sizeof(_data)) {
    return false;
  }
  NamespaceHeader header{};
  memcpy(&header, _data, sizeof(header));
  header.used = _length;
  memcpy(_data, &header, sizeof(header));
  return cinv_item_write(_id, _length, _data) == CINV_OPER_SUCCESS;
}

bool Preferences::validStoredValue(uint8_t type, const uint8_t *value,
                                   size_t length) {
  if (value == nullptr || length == 0) {
    return false;
  }
  switch (static_cast<StoredType>(type)) {
    case StoredType::U8:
    case StoredType::I8:
      return length == sizeof(uint8_t);
    case StoredType::U16:
    case StoredType::I16:
      return length == sizeof(uint16_t);
    case StoredType::U32:
    case StoredType::I32:
      return length == sizeof(uint32_t);
    case StoredType::U64:
    case StoredType::I64:
      return length == sizeof(uint64_t);
    case StoredType::String:
      return value[length - 1] == '\0' &&
             memchr(value, '\0', length - 1) == nullptr;
    case StoredType::Blob:
      return true;
  }
  return false;
}

PreferenceType Preferences::publicType(uint8_t storedType) {
  switch (static_cast<StoredType>(storedType)) {
    case StoredType::I8:
      return PT_I8;
    case StoredType::U8:
      return PT_U8;
    case StoredType::I16:
      return PT_I16;
    case StoredType::U16:
      return PT_U16;
    case StoredType::I32:
      return PT_I32;
    case StoredType::U32:
      return PT_U32;
    case StoredType::I64:
      return PT_I64;
    case StoredType::U64:
      return PT_U64;
    case StoredType::String:
      return PT_STR;
    case StoredType::Blob:
      return PT_BLOB;
  }
  return PT_INVALID;
}

bool Preferences::entryAt(size_t offset, EntryView *entry) const {
  if (entry == nullptr || offset < sizeof(NamespaceHeader) ||
      offset + sizeof(EntryHeader) > _length) {
    return false;
  }
  EntryHeader header{};
  // Entries are intentionally packed to maximize the 240-byte NVDM item.
  // Copy the header because a preceding key/value can leave this offset odd,
  // and CI130X does not emulate misaligned half-word loads.
  memcpy(&header, _data + offset, sizeof(header));
  if (header.keyLength == 0 || header.keyLength > kMaxKeyLength) {
    return false;
  }
  const size_t total = sizeof(EntryHeader) + header.keyLength +
                       static_cast<size_t>(header.valueLength);
  if (total > _length - offset) {
    return false;
  }
  const uint8_t *key = _data + offset + sizeof(EntryHeader);
  const uint8_t *value = key + header.keyLength;
  if (memchr(key, '\0', header.keyLength) != nullptr ||
      !validStoredValue(header.type, value, header.valueLength)) {
    return false;
  }
  entry->offset = offset;
  entry->totalLength = total;
  entry->header = header;
  entry->value = value;
  return true;
}

size_t Preferences::entryCount() const {
  size_t count = 0;
  size_t offset = sizeof(NamespaceHeader);
  while (offset < _length) {
    EntryView entry{};
    if (!entryAt(offset, &entry)) {
      return kMaxEntries + 1;
    }
    if (count >= kMaxEntries) {
      return kMaxEntries + 1;
    }

    const uint8_t *key = _data + offset + sizeof(EntryHeader);
    size_t previousOffset = sizeof(NamespaceHeader);
    while (previousOffset < offset) {
      EntryView previous{};
      if (!entryAt(previousOffset, &previous)) {
        return kMaxEntries + 1;
      }
      const uint8_t *previousKey =
          _data + previousOffset + sizeof(EntryHeader);
      if (previous.header.keyLength == entry.header.keyLength &&
          memcmp(previousKey, key, entry.header.keyLength) == 0) {
        return kMaxEntries + 1;
      }
      previousOffset += previous.totalLength;
    }

    ++count;
    offset += entry.totalLength;
  }
  return offset == _length ? count : kMaxEntries + 1;
}

bool Preferences::findEntry(const char *key, EntryView *entry) {
  size_t keyLength = 0;
  if (!validKey(key, &keyLength)) {
    return false;
  }
  size_t offset = sizeof(NamespaceHeader);
  while (offset < _length) {
    EntryView current{};
    if (!entryAt(offset, &current)) {
      return false;
    }
    const uint8_t *storedKey = _data + offset + sizeof(EntryHeader);
    if (current.header.keyLength == keyLength &&
        memcmp(storedKey, key, keyLength) == 0) {
      if (entry != nullptr) {
        *entry = current;
      }
      return true;
    }
    offset += current.totalLength;
  }
  return false;
}

size_t Preferences::putValue(const char *key, StoredType type,
                             const void *value, size_t length) {
  PreferencesLock lock;
  if (!lock.locked()) {
    return 0;
  }
  size_t keyLength = 0;
  if (!_begun || _readOnly || value == nullptr || length == 0 ||
      length > UINT16_MAX || !validKey(key, &keyLength) ||
      !validStoredValue(static_cast<uint8_t>(type),
                        static_cast<const uint8_t *>(value), length) ||
      !load()) {
    return 0;
  }

  EntryView oldEntry{};
  const bool replacing = findEntry(key, &oldEntry);
  const size_t oldLength = replacing ? oldEntry.totalLength : 0;
  if (!replacing && entryCount() >= kMaxEntries) {
    return 0;
  }
  const size_t newEntryLength = sizeof(EntryHeader) + keyLength + length;
  if (newEntryLength > sizeof(_data) ||
      _length - oldLength > sizeof(_data) - newEntryLength) {
    return 0;
  }

  if (replacing && oldEntry.header.type == static_cast<uint8_t>(type) &&
      oldEntry.header.valueLength == length &&
      memcmp(oldEntry.value, value, length) == 0) {
    return length;
  }

  if (replacing) {
    const size_t tailOffset = oldEntry.offset + oldEntry.totalLength;
    memmove(_data + oldEntry.offset, _data + tailOffset,
            _length - tailOffset);
    _length -= oldEntry.totalLength;
  }

  EntryHeader header{};
  header.keyLength = static_cast<uint8_t>(keyLength);
  header.type = static_cast<uint8_t>(type);
  header.valueLength = static_cast<uint16_t>(length);
  memcpy(_data + _length, &header, sizeof(header));
  memcpy(_data + _length + sizeof(header), key, keyLength);
  memcpy(_data + _length + sizeof(header) + keyLength, value, length);
  _length += newEntryLength;
  return store() ? length : 0;
}

bool Preferences::getBlobUnlocked(const char *key, StoredType type,
                                  const uint8_t **value, size_t *length) {
  if (value == nullptr || length == nullptr) {
    return false;
  }
  EntryView entry{};
  if (!findEntry(key, &entry) ||
      entry.header.type != static_cast<uint8_t>(type)) {
    return false;
  }
  *value = entry.value;
  *length = entry.header.valueLength;
  return true;
}

bool Preferences::getValue(const char *key, StoredType type, void *value,
                           size_t length) {
  PreferencesLock lock;
  if (!lock.locked() || !_begun || !load()) {
    return false;
  }
  const uint8_t *stored = nullptr;
  size_t storedLength = 0;
  if (value == nullptr ||
      !getBlobUnlocked(key, type, &stored, &storedLength) ||
      storedLength != length) {
    return false;
  }
  memcpy(value, stored, length);
  return true;
}

bool Preferences::clear() {
  PreferencesLock lock;
  if (!lock.locked() || !_begun || _readOnly || !load()) {
    return false;
  }
  _length = sizeof(NamespaceHeader);
  return store();
}

bool Preferences::remove(const char *key) {
  PreferencesLock lock;
  if (!lock.locked() || !_begun || _readOnly || !load()) {
    return false;
  }
  EntryView entry{};
  if (!findEntry(key, &entry)) {
    return false;
  }
  const size_t tailOffset = entry.offset + entry.totalLength;
  memmove(_data + entry.offset, _data + tailOffset, _length - tailOffset);
  _length -= entry.totalLength;
  return store();
}

bool Preferences::isKey(const char *key) {
  PreferencesLock lock;
  if (!lock.locked() || !_begun || !load()) {
    return false;
  }
  return findEntry(key, nullptr);
}

PreferenceType Preferences::getType(const char *key) {
  PreferencesLock lock;
  if (!lock.locked() || !_begun || !load()) {
    return PT_INVALID;
  }
  EntryView entry{};
  return findEntry(key, &entry) ? publicType(entry.header.type) : PT_INVALID;
}

size_t Preferences::freeEntries() {
  PreferencesLock lock;
  if (!lock.locked() || !_begun || !load()) {
    return 0;
  }
  const size_t count = entryCount();
  return count <= kMaxEntries ? kMaxEntries - count : 0;
}

bool Preferences::isReadOnly() const {
  PreferencesLock lock;
  return lock.locked() && _begun && _readOnly;
}

bool Preferences::isBegun() const {
  PreferencesLock lock;
  return lock.locked() && _begun;
}

#define PREFERENCES_PUT_SCALAR(method, cppType, prefType)                 \
  size_t Preferences::method(const char *key, cppType value) {            \
    return putValue(key, prefType, &value, sizeof(value));                 \
  }

PREFERENCES_PUT_SCALAR(putChar, int8_t, StoredType::I8)
PREFERENCES_PUT_SCALAR(putUChar, uint8_t, StoredType::U8)
PREFERENCES_PUT_SCALAR(putShort, int16_t, StoredType::I16)
PREFERENCES_PUT_SCALAR(putUShort, uint16_t, StoredType::U16)
PREFERENCES_PUT_SCALAR(putInt, int32_t, StoredType::I32)
PREFERENCES_PUT_SCALAR(putUInt, uint32_t, StoredType::U32)
PREFERENCES_PUT_SCALAR(putLong, int32_t, StoredType::I32)
PREFERENCES_PUT_SCALAR(putULong, uint32_t, StoredType::U32)
PREFERENCES_PUT_SCALAR(putLong64, int64_t, StoredType::I64)
PREFERENCES_PUT_SCALAR(putULong64, uint64_t, StoredType::U64)

#undef PREFERENCES_PUT_SCALAR

size_t Preferences::putFloat(const char *key, float value) {
  return putBytes(key, &value, sizeof(value));
}

size_t Preferences::putDouble(const char *key, double value) {
  return putBytes(key, &value, sizeof(value));
}

size_t Preferences::putBool(const char *key, bool value) {
  const uint8_t stored = value ? 1U : 0U;
  return putUChar(key, stored);
}

size_t Preferences::putString(const char *key, const char *value) {
  if (value == nullptr) {
    return 0;
  }
  const size_t length = strlen(value) + 1;
  return putValue(key, StoredType::String, value, length) == length
             ? length - 1
             : 0;
}

size_t Preferences::putString(const char *key, const String &value) {
  return putString(key, value.c_str());
}

size_t Preferences::putBytes(const char *key, const void *value,
                             size_t length) {
  return putValue(key, StoredType::Blob, value, length);
}

#define PREFERENCES_GET_SCALAR(method, cppType, prefType)                  \
  cppType Preferences::method(const char *key, cppType defaultValue) {     \
    cppType value{};                                                        \
    return getValue(key, prefType, &value, sizeof(value)) ? value           \
                                                           : defaultValue;  \
  }

PREFERENCES_GET_SCALAR(getChar, int8_t, StoredType::I8)
PREFERENCES_GET_SCALAR(getUChar, uint8_t, StoredType::U8)
PREFERENCES_GET_SCALAR(getShort, int16_t, StoredType::I16)
PREFERENCES_GET_SCALAR(getUShort, uint16_t, StoredType::U16)
PREFERENCES_GET_SCALAR(getInt, int32_t, StoredType::I32)
PREFERENCES_GET_SCALAR(getUInt, uint32_t, StoredType::U32)
PREFERENCES_GET_SCALAR(getLong, int32_t, StoredType::I32)
PREFERENCES_GET_SCALAR(getULong, uint32_t, StoredType::U32)
PREFERENCES_GET_SCALAR(getLong64, int64_t, StoredType::I64)
PREFERENCES_GET_SCALAR(getULong64, uint64_t, StoredType::U64)

#undef PREFERENCES_GET_SCALAR

float Preferences::getFloat(const char *key, float defaultValue) {
  float value{};
  return getValue(key, StoredType::Blob, &value, sizeof(value))
             ? value
             : defaultValue;
}

double Preferences::getDouble(const char *key, double defaultValue) {
  double value{};
  return getValue(key, StoredType::Blob, &value, sizeof(value))
             ? value
             : defaultValue;
}

bool Preferences::getBool(const char *key, bool defaultValue) {
  uint8_t value = 0;
  return getValue(key, StoredType::U8, &value, sizeof(value))
             ? value == 1U
             : defaultValue;
}

size_t Preferences::getString(const char *key, char *value,
                              size_t maxLength) {
  PreferencesLock lock;
  if (!lock.locked() || !_begun || !load()) {
    return 0;
  }
  const uint8_t *stored = nullptr;
  size_t length = 0;
  if (value == nullptr || maxLength == 0 ||
      !getBlobUnlocked(key, StoredType::String, &stored, &length) ||
      length == 0 ||
      stored[length - 1] != '\0' || length > maxLength) {
    return 0;
  }
  memcpy(value, stored, length);
  return length;
}

String Preferences::getString(const char *key, const String &defaultValue) {
  PreferencesLock lock;
  if (!lock.locked() || !_begun || !load()) {
    return defaultValue;
  }
  const uint8_t *stored = nullptr;
  size_t length = 0;
  if (!getBlobUnlocked(key, StoredType::String, &stored, &length) ||
      length == 0 ||
      stored[length - 1] != '\0') {
    return defaultValue;
  }
  return String(reinterpret_cast<const char *>(stored));
}

size_t Preferences::getStringLength(const char *key) {
  PreferencesLock lock;
  if (!lock.locked() || !_begun || !load()) {
    return 0;
  }
  const uint8_t *stored = nullptr;
  size_t length = 0;
  if (!getBlobUnlocked(key, StoredType::String, &stored, &length) ||
      length == 0 ||
      stored[length - 1] != '\0') {
    return 0;
  }
  return length;
}

size_t Preferences::getBytesLength(const char *key) {
  PreferencesLock lock;
  if (!lock.locked() || !_begun || !load()) {
    return 0;
  }
  const uint8_t *stored = nullptr;
  size_t length = 0;
  return getBlobUnlocked(key, StoredType::Blob, &stored, &length) ? length
                                                                  : 0;
}

size_t Preferences::getBytes(const char *key, void *value,
                             size_t maxLength) {
  PreferencesLock lock;
  if (!lock.locked() || !_begun || !load()) {
    return 0;
  }
  const uint8_t *stored = nullptr;
  size_t length = 0;
  if (!getBlobUnlocked(key, StoredType::Blob, &stored, &length)) {
    return 0;
  }
  if (value == nullptr || maxLength == 0) {
    return length;
  }
  if (length > maxLength) {
    return 0;
  }
  memcpy(value, stored, length);
  return length;
}
