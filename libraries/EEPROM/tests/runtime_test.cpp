// Executes the production EEPROM implementation with fault-injected SDK calls.
// ci_nvdata_manage.h comes from tools/sdk/include, not a copied fake header.
#include <Arduino.h>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <type_traits>
#include <vector>

void *eeprom_test_malloc(size_t size);
void eeprom_test_free(void *pointer);

// Only EEPROM's allocation is intercepted; the harness uses the host allocator.
#define malloc eeprom_test_malloc
#define free eeprom_test_free
#include "../src/EEPROM.cpp"
#undef malloc
#undef free

static_assert(!std::is_copy_constructible<EEPROMClass>::value,
              "EEPROM buffers must not be shallow copied");
static_assert(!std::is_copy_assignable<EEPROMClass>::value,
              "EEPROM buffers must not be shallow assigned");
static_assert(std::is_same<decltype(EEPROM.end()), bool>::value,
              "end() must report whether persistence succeeded");

namespace {
unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
  std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
  std::abort(); } } while (0)

constexpr uint32_t itemId = 0x60454550UL;
constexpr size_t capacity = 240;
constexpr size_t guardSize = 16;
struct Allocation { uint8_t *base; size_t size; };
std::map<void *, Allocation> allocations;

struct FakeSDK {
  std::vector<uint8_t> stored;
  bool exists = false;
  bool sdkBeginOk = true;
  bool failAllocation = false;
  bool dropWrite = false;
  bool corruptRead = false;
  size_t corruptReadIndex = 0;
  uint32_t now = 0;
  uint32_t waited = 0;
  uint32_t readyAfter = 0;
  uint32_t failAfter = UINT32_MAX;
  chipintelli_sdk_state_t state = CHIPINTELLI_SDK_STARTING;
  int32_t trap = 0;
  int initOverride = -1;
  int readOverride = -1;
  int actualOverride = -1;
  cinv_item_ret_t writeResult = CINV_OPER_SUCCESS;
  unsigned sdkStarts = 0;
  unsigned readyQueries = 0;
  unsigned initCalls = 0;
  unsigned readCalls = 0;
  unsigned writeCalls = 0;
  unsigned mallocCalls = 0;
  unsigned freeCalls = 0;
} sdk;

void reset() {
  CHECK(allocations.empty());
  sdk = FakeSDK();
}

void seed(size_t length) {
  sdk.exists = true;
  sdk.stored.resize(length);
  for (size_t i = 0; i < length; ++i) {
    sdk.stored[i] = static_cast<uint8_t>((i * 37 + 11) & 0xff);
  }
}

void checkClosed(const EEPROMClass &value) {
  CHECK(!value.isBegun());
  CHECK(value.length() == 0);
  CHECK(allocations.empty());
}

void checkRange(const EEPROMClass &value, size_t from, size_t to, uint8_t byte) {
  for (size_t i = from; i < to; ++i) {
    CHECK(value.read(static_cast<int>(i)) == byte);
  }
}

void testUnopened() {
  reset();
  EEPROMClass value;
  uint32_t original = 0x12345678;
  const uint32_t replacement = 42;
  checkClosed(value);
  CHECK(value.read(0) == 0xff);
  CHECK(value.read(-1) == 0xff);
  CHECK(value.readBytes(0, &original, sizeof(original)) == 0);
  CHECK(value.writeBytes(0, &replacement, sizeof(replacement)) == 0);
  CHECK(&value.get(0, original) == &original);
  CHECK(original == 0x12345678);
  CHECK(&value.put(0, replacement) == &replacement);
  value.write(0, 1);
  value.update(0, 2);
  CHECK(!value.commit());
  CHECK(value.end());
  CHECK(sdk.sdkStarts == 0 && sdk.initCalls == 0 && sdk.writeCalls == 0);
}

void testDefaultAndAllSizes() {
  reset();
  {
    EEPROMClass value;
    CHECK(value.begin());
    CHECK(value.length() == 128);
    CHECK(sdk.initCalls == 1 && sdk.readCalls == 1);
    CHECK(sdk.stored.size() == 128);
    checkRange(value, 0, 128, 0xff);
    CHECK(value.begin(capacity));
    checkRange(value, 0, capacity, 0xff);
    CHECK(sdk.initCalls == 1 && sdk.readCalls == 1);
    CHECK(value.commit());
    CHECK(sdk.writeCalls == 0);
    CHECK(value.end());
    checkClosed(value);
  }
  for (size_t size = 1; size <= capacity; ++size) {
    reset();
    EEPROMClass value;
    CHECK(value.begin(size));
    CHECK(value.length() == size);
    CHECK(sdk.stored.size() == size);
    value.write(static_cast<int>(size - 1), 0x42);
    CHECK(value.end());
    CHECK(sdk.stored[size - 1] == 0x42 && sdk.stored.size() == size);
    CHECK(sdk.writeCalls == 1 && sdk.readCalls == 2);
  }
}

void testInvalidSizesPreserveState() {
  reset();
  EEPROMClass value;
  const size_t invalid[] = {0, capacity + 1, std::numeric_limits<size_t>::max()};
  for (size_t size : invalid) CHECK(!value.begin(size));
  checkClosed(value);
  CHECK(sdk.sdkStarts == 0);
  CHECK(value.begin(16));
  value.write(15, 0x25);
  for (size_t size : invalid) {
    CHECK(!value.begin(size));
    CHECK(value.length() == 16 && value.read(15) == 0x25);
  }
  CHECK(sdk.initCalls == 1 && sdk.writeCalls == 0);
  CHECK(value.end());
  CHECK(sdk.stored[15] == 0x25);
}

void testExistingFullRecordSmallWindow() {
  reset();
  seed(capacity);
  const std::vector<uint8_t> original = sdk.stored;
  EEPROMClass value;
  CHECK(value.begin(1));
  CHECK(value.read(0) == original[0]);
  CHECK(value.begin(capacity));
  for (size_t i = 0; i < capacity; ++i) {
    CHECK(value.read(static_cast<int>(i)) == original[i]);
  }
  CHECK(value.begin(1));
  value.write(0, 0xa5);
  CHECK(value.end());
  CHECK(sdk.stored[0] == 0xa5);
  CHECK(std::equal(original.begin() + 1, original.end(), sdk.stored.begin() + 1));
  CHECK(value.begin(capacity));
  CHECK(value.read(239) == original[239]);
}

void testLegacyShortRecords() {
  const size_t lengths[] = {1, 7, 127, 128, 239};
  for (size_t length : lengths) {
    reset();
    seed(length);
    const std::vector<uint8_t> original = sdk.stored;
    EEPROMClass value;
    CHECK(value.begin(capacity));
    for (size_t i = 0; i < length; ++i) {
      CHECK(value.read(static_cast<int>(i)) == original[i]);
    }
    checkRange(value, length, capacity, 0xff);
    CHECK(value.commit());
    CHECK(sdk.writeCalls == 0 && sdk.stored.size() == length);
    CHECK(value.begin(1));
    value.write(0, 0x91);
    CHECK(value.end());
    CHECK(sdk.stored.size() == capacity);
    CHECK(std::equal(original.begin() + 1, original.end(), sdk.stored.begin() + 1));
    for (size_t i = length; i < capacity; ++i) CHECK(sdk.stored[i] == 0xff);
  }
}

void testResizePreservesDirtyTail() {
  reset();
  EEPROMClass value;
  CHECK(value.begin(capacity));
  value.write(239, 0x35);
  value.write(0, 0x61);
  CHECK(value.begin(1));
  CHECK(value.read(239) == 0xff);
  value.write(239, 0x99);
  sdk.sdkBeginOk = false;  // Resize must use the existing buffer directly.
  CHECK(value.begin(1));
  CHECK(value.begin(capacity));
  CHECK(value.read(239) == 0x35 && value.read(0) == 0x61);
  CHECK(sdk.sdkStarts == 1 && sdk.initCalls == 1 && sdk.readCalls == 1);
  CHECK(sdk.mallocCalls == 1 && sdk.freeCalls == 0 && sdk.writeCalls == 0);
  CHECK(value.begin(1));
  CHECK(value.end());
  CHECK(sdk.stored[239] == 0x35 && sdk.stored[0] == 0x61);
  sdk.sdkBeginOk = true;
  CHECK(value.begin(capacity));
  CHECK(value.read(239) == 0x35 && value.read(0) == 0x61);
}

void testUnchangedWrites() {
  reset();
  EEPROMClass value;
  CHECK(value.begin(8));
  const uint8_t erased[] = {0xff, 0xff, 0xff};
  value.write(0, 0xff);
  value.update(1, 0xff);
  CHECK(value.writeBytes(2, erased, sizeof(erased)) == sizeof(erased));
  CHECK(value.commit());
  CHECK(sdk.writeCalls == 0 && sdk.readCalls == 1);
  value.write(0, 0x31);
  CHECK(value.commit());
  CHECK(sdk.writeCalls == 1 && sdk.readCalls == 2);
  value.write(0, 0x31);
  value.update(0, 0x31);
  const uint8_t same = 0x31;
  CHECK(value.writeBytes(0, &same, 1) == 1);
  CHECK(value.commit());
  CHECK(sdk.writeCalls == 1 && sdk.readCalls == 2);
}

void testStorageHighWaterMark() {
  reset();
  EEPROMClass value;
  CHECK(value.begin(1));
  value.write(0, 0x13);
  CHECK(value.commit());
  CHECK(sdk.stored.size() == 1);
  CHECK(value.begin(16));
  value.write(15, 0x27);
  CHECK(value.begin(4));
  CHECK(value.commit());
  CHECK(sdk.stored.size() == 16 && sdk.stored[15] == 0x27);
  CHECK(value.end());
  CHECK(value.begin(1));
  value.write(0, 0x31);
  CHECK(value.end());
  CHECK(sdk.stored.size() == 16 && sdk.stored[15] == 0x27);
  CHECK(value.begin(32));
  checkRange(value, 16, 32, 0xff);
  CHECK(value.begin(1));
  value.write(0, 0x53);
  CHECK(value.end());
  CHECK(sdk.stored.size() == 32 && sdk.stored[15] == 0x27);
  for (size_t i = 16; i < 32; ++i) CHECK(sdk.stored[i] == 0xff);
  seed(64);
  const std::vector<uint8_t> original = sdk.stored;
  CHECK(value.begin(128));
  CHECK(value.begin(32));
  value.write(0, 0x91);
  CHECK(value.end());
  CHECK(sdk.stored.size() == 128);
  CHECK(std::equal(original.begin() + 1, original.end(), sdk.stored.begin() + 1));
  for (size_t i = 64; i < 128; ++i) CHECK(sdk.stored[i] == 0xff);
}

void testRangesAndTypedAccess() {
  reset();
  EEPROMClass value;
  CHECK(value.begin(8));
  const uint8_t bytes[] = {1, 2, 3, 4, 5, 6, 7, 8};
  CHECK(value.writeBytes(0, bytes, sizeof(bytes)) == sizeof(bytes));
  CHECK(value.commit());
  uint8_t output[8];
  std::memset(output, 0x5a, sizeof(output));
  CHECK(value.readBytes(1, output, sizeof(output)) == 0);
  for (uint8_t byte : output) CHECK(byte == 0x5a);
  const int invalidAddresses[] = {-1, 8, 9, INT_MAX};
  for (int address : invalidAddresses) {
    CHECK(value.read(address) == 0xff);
    value.write(address, 0x80);
    value.update(address, 0x81);
    CHECK(value.readBytes(address, output, 1) == 0);
    CHECK(value.writeBytes(address, bytes, 1) == 0);
  }
  CHECK(value.readBytes(0, nullptr, 1) == 0);
  CHECK(value.writeBytes(0, nullptr, 1) == 0);
  CHECK(value.readBytes(1, output, std::numeric_limits<size_t>::max()) == 0);
  CHECK(value.writeBytes(1, bytes, std::numeric_limits<size_t>::max()) == 0);
  CHECK(value.readBytes(8, output, 0) == 0);
  CHECK(value.writeBytes(8, bytes, 0) == 0);
  CHECK(value.readBytes(0, output, sizeof(output)) == sizeof(output));
  CHECK(std::memcmp(output, bytes, sizeof(bytes)) == 0);
  CHECK(value.commit() && sdk.writeCalls == 1);
  const uint32_t typed = 0x31415926;
  uint32_t loaded = 0;
  CHECK(&value.put(4, typed) == &typed);
  CHECK(&value.get(4, loaded) == &loaded && loaded == typed);
  CHECK(&value.get(5, loaded) == &loaded && loaded == typed);
  const uint32_t invalid = 0x87654321;
  CHECK(&value.put(5, invalid) == &invalid);
  CHECK(value.get(4, loaded) == typed);
}

void testSDKStartupFailures() {
  reset();
  EEPROMClass value;
  sdk.sdkBeginOk = false;
  CHECK(!value.begin());
  checkClosed(value);
  CHECK(sdk.sdkStarts == 1 && sdk.readyQueries == 0 && sdk.initCalls == 0);
  sdk.sdkBeginOk = true;
  sdk.readyAfter = 20;
  sdk.state = CHIPINTELLI_SDK_FAILED;
  CHECK(!value.begin());
  checkClosed(value);
  CHECK(sdk.waited == 0 && sdk.initCalls == 0);
  sdk.state = CHIPINTELLI_SDK_STARTING;
  sdk.failAfter = 3;
  CHECK(!value.begin());
  CHECK(sdk.waited == 3 && sdk.initCalls == 0);
  sdk.failAfter = UINT32_MAX;
  sdk.state = CHIPINTELLI_SDK_STARTING;
  CHECK(value.begin());
  CHECK(sdk.waited == 20);
}

void testTimeoutAndClockWrap() {
  const uint32_t starts[] = {0, UINT32_MAX - 5};
  for (uint32_t start : starts) {
    reset();
    EEPROMClass value;
    sdk.now = start;
    sdk.readyAfter = 10001;
    CHECK(!value.begin());
    checkClosed(value);
    CHECK(sdk.waited == 10000 && sdk.initCalls == 0 && sdk.mallocCalls == 0);
    sdk.readyAfter = 10004;
    CHECK(value.begin());
    CHECK(sdk.waited == 10004);
  }
}

void testAllocationFailure() {
  reset();
  EEPROMClass value;
  sdk.failAllocation = true;
  CHECK(!value.begin());
  checkClosed(value);
  CHECK(sdk.mallocCalls == 1 && sdk.initCalls == 0 && sdk.readCalls == 0);
  sdk.failAllocation = false;
  CHECK(value.begin());
  CHECK(value.end());
  CHECK(sdk.freeCalls == 1);
}

void testInitFailures() {
  const int results[] = {CINV_OPER_FAILED, CINV_ITEM_LEN_ERR, 99};
  for (int result : results) {
    reset();
    EEPROMClass value;
    sdk.initOverride = result;
    CHECK(!value.begin());
    checkClosed(value);
    CHECK(sdk.initCalls == 1 && sdk.readCalls == 0 && sdk.freeCalls == 1);
    sdk.initOverride = -1;
    CHECK(value.begin());
  }
}

void testReadFailures() {
  const int results[] = {CINV_ITEM_UNINIT, CINV_OPER_FAILED, CINV_ITEM_LEN_ERR, 99};
  for (int result : results) {
    reset();
    EEPROMClass value;
    sdk.readOverride = result;
    CHECK(!value.begin());
    checkClosed(value);
    CHECK(sdk.freeCalls == 1 && sdk.writeCalls == 0);
    sdk.readOverride = -1;
    CHECK(value.begin());
  }
  const int lengths[] = {0, 241, UINT16_MAX};
  for (int length : lengths) {
    reset();
    EEPROMClass value;
    sdk.actualOverride = length;
    CHECK(!value.begin());
    checkClosed(value);
    sdk.actualOverride = -1;
    CHECK(value.begin());
  }
}

void testWriteFailuresRetry() {
  const cinv_item_ret_t results[] = {
    CINV_ITEM_UNINIT, CINV_OPER_FAILED, CINV_ITEM_LEN_ERR
  };
  for (cinv_item_ret_t result : results) {
    reset();
    EEPROMClass value;
    CHECK(value.begin(8));
    value.write(7, 0x76);
    sdk.writeResult = result;
    CHECK(!value.commit());
    CHECK(value.isBegun() && value.read(7) == 0x76);
    CHECK(sdk.stored[7] == 0xff && sdk.readCalls == 1);
    CHECK(!value.end());
    CHECK(value.isBegun() && value.length() == 8 && sdk.freeCalls == 0);
    sdk.writeResult = CINV_OPER_SUCCESS;
    CHECK(value.commit());
    CHECK(sdk.writeCalls == 3 && sdk.stored[7] == 0x76);
    CHECK(value.commit() && sdk.writeCalls == 3);
    CHECK(value.end());
    checkClosed(value);
  }
}

void testVerificationFailuresRetry() {
  // Each failure happens after cinv_item_write has returned success.
  for (int failure = 0; failure < 8; ++failure) {
    reset();
    EEPROMClass value;
    CHECK(value.begin(8));
    value.write(7, 0x76);
    if (failure == 0) sdk.readOverride = CINV_OPER_FAILED;
    if (failure == 1) sdk.readOverride = CINV_ITEM_UNINIT;
    if (failure == 2) sdk.actualOverride = 0;
    if (failure == 3) sdk.actualOverride = 4;
    if (failure == 4) sdk.actualOverride = 241;
    if (failure == 5) sdk.corruptRead = true;
    if (failure == 6) sdk.dropWrite = true;
    if (failure == 7) {
      CHECK(value.begin(1));
      sdk.corruptRead = true;
      sdk.corruptReadIndex = 7;  // Verify bytes hidden by a later logical shrink.
    }
    CHECK(!value.commit());
    CHECK(value.isBegun());
    CHECK(value.begin(8));
    CHECK(value.read(7) == 0x76);
    CHECK(sdk.writeCalls == 1 && sdk.readCalls == 2);
    CHECK(!value.end());
    CHECK(value.isBegun() && sdk.freeCalls == 0);
    sdk.readOverride = -1;
    sdk.actualOverride = -1;
    sdk.corruptRead = false;
    sdk.dropWrite = false;
    CHECK(value.end());
    CHECK(sdk.writeCalls == 3 && sdk.stored[7] == 0x76);
    checkClosed(value);
  }
}

void testInterruptGuards() {
  reset();
  EEPROMClass value;
  sdk.trap = 1;
  CHECK(!value.begin());
  CHECK(!value.commit());
  CHECK(value.end());
  CHECK(sdk.sdkStarts == 0 && sdk.initCalls == 0);
  sdk.trap = 0;
  CHECK(value.begin(8));
  sdk.trap = 1;
  CHECK(!value.commit());
  CHECK(!value.end());
  CHECK(!value.begin(1));
  CHECK(value.length() == 8 && value.isBegun());
  CHECK(sdk.writeCalls == 0 && sdk.freeCalls == 0);
  sdk.trap = 0;
  value.write(7, 0x73);
  sdk.trap = 1;
  CHECK(!value.commit() && !value.end());
  CHECK(value.read(7) == 0x73 && sdk.writeCalls == 0);
  sdk.trap = 0;
  CHECK(value.end());
  CHECK(sdk.stored[7] == 0x73);
}

void testDestructorCleanup() {
  for (int mode = 0; mode < 4; ++mode) {
    reset();
    {
      EEPROMClass value;
      CHECK(value.begin());
      if (mode != 0) value.write(0, 0x87);
      if (mode == 2) sdk.writeResult = CINV_OPER_FAILED;
      if (mode == 3) sdk.trap = 1;
    }
    CHECK(allocations.empty() && sdk.freeCalls == 1);
    CHECK(sdk.writeCalls == ((mode == 1 || mode == 2) ? 1U : 0U));
    CHECK(sdk.stored[0] == (mode == 1 ? 0x87 : 0xff));
  }
}
}  // namespace

void *eeprom_test_malloc(size_t size) {
  ++sdk.mallocCalls;
  CHECK(size == capacity);
  if (sdk.failAllocation) return nullptr;
  uint8_t *base = static_cast<uint8_t *>(std::malloc(size + 2 * guardSize));
  CHECK(base != nullptr);
  std::memset(base, 0xa7, size + 2 * guardSize);
  void *pointer = base + guardSize;
  allocations[pointer] = Allocation{base, size};
  return pointer;
}

void eeprom_test_free(void *pointer) {
  if (pointer == nullptr) return;
  const auto found = allocations.find(pointer);
  CHECK(found != allocations.end());
  const Allocation allocation = found->second;
  for (size_t i = 0; i < guardSize; ++i) {
    CHECK(allocation.base[i] == 0xa7);
    CHECK(allocation.base[guardSize + allocation.size + i] == 0xa7);
  }
  std::free(allocation.base);
  allocations.erase(found);
  ++sdk.freeCalls;
}

extern "C" {
unsigned long millis(void) { return sdk.now; }

void delay(unsigned long milliseconds) {
  CHECK(sdk.trap == 0);
  CHECK(milliseconds == 1);
  sdk.now += static_cast<uint32_t>(milliseconds);
  sdk.waited += static_cast<uint32_t>(milliseconds);
  if (sdk.waited >= sdk.failAfter) sdk.state = CHIPINTELLI_SDK_FAILED;
  CHECK(sdk.waited <= 20005);  // Fail a broken timeout instead of hanging.
}

bool chipintelli_sdk_begin(void) {
  CHECK(sdk.trap == 0);
  ++sdk.sdkStarts;
  return sdk.sdkBeginOk;
}

chipintelli_sdk_state_t chipintelli_sdk_state(void) { return sdk.state; }

void is_ci_flash_data_info_inited(bool *state) {
  CHECK(sdk.trap == 0 && sdk.sdkStarts != 0);
  ++sdk.readyQueries;
  *state = sdk.waited >= sdk.readyAfter;
}

int32_t check_curr_trap(void) { return sdk.trap; }

cinv_item_ret_t cinv_item_init(uint32_t id, uint16_t length, void *buffer) {
  CHECK(sdk.trap == 0 && id == itemId && length > 0 && length <= capacity);
  ++sdk.initCalls;
  if (sdk.initOverride >= 0) return static_cast<cinv_item_ret_t>(sdk.initOverride);
  if (sdk.exists) return CINV_OPER_SUCCESS;
  const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
  sdk.stored.assign(bytes, bytes + length);
  sdk.exists = true;
  return CINV_ITEM_UNINIT;  // The SDK uses UNINIT for successful creation.
}

cinv_item_ret_t cinv_item_read(uint32_t id, uint16_t length, void *buffer,
                               uint16_t *actual) {
  CHECK(sdk.trap == 0 && id == itemId);
  ++sdk.readCalls;
  if (sdk.readOverride >= 0) return static_cast<cinv_item_ret_t>(sdk.readOverride);
  if (!sdk.exists) return CINV_ITEM_UNINIT;
  // Model the SDK's whole-record checksum requirement on an existing item.
  if (length < sdk.stored.size()) return CINV_OPER_FAILED;
  CHECK(length == capacity);
  const size_t copied = std::min<size_t>(length, sdk.stored.size());
  std::memcpy(buffer, sdk.stored.data(), copied);
  if (sdk.corruptRead && sdk.corruptReadIndex < copied) {
    static_cast<uint8_t *>(buffer)[sdk.corruptReadIndex] ^= 0x01;
  }
  *actual = static_cast<uint16_t>(sdk.actualOverride >= 0
      ? sdk.actualOverride : copied);
  return CINV_OPER_SUCCESS;
}

cinv_item_ret_t cinv_item_write(uint32_t id, uint16_t length, void *buffer) {
  CHECK(sdk.trap == 0 && id == itemId && length > 0 && length <= capacity);
  ++sdk.writeCalls;
  if (sdk.writeResult != CINV_OPER_SUCCESS) return sdk.writeResult;
  if (!sdk.exists) return CINV_ITEM_UNINIT;
  if (!sdk.dropWrite) {
    const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
    sdk.stored.assign(bytes, bytes + length);
  }
  return CINV_OPER_SUCCESS;
}
}  // extern "C"

int main() {
  struct Test { const char *name; void (*run)(); };
  const Test tests[] = {
    {"unopened API", testUnopened},
    {"default and all supported sizes", testDefaultAndAllSizes},
    {"invalid sizes preserve state", testInvalidSizesPreserveState},
    {"full record with small logical window", testExistingFullRecordSmallWindow},
    {"legacy short records", testLegacyShortRecords},
    {"resize preserves dirty hidden tail", testResizePreservesDirtyTail},
    {"unchanged writes avoid flash", testUnchangedWrites},
    {"storage grows only to the historical accessible range", testStorageHighWaterMark},
    {"ranges and typed access", testRangesAndTypedAccess},
    {"SDK startup failure and retry", testSDKStartupFailures},
    {"readiness timeout and millis wrap", testTimeoutAndClockWrap},
    {"allocation failure and retry", testAllocationFailure},
    {"NVDM init failures", testInitFailures},
    {"NVDM read failures and invalid lengths", testReadFailures},
    {"write failures preserve retryable data", testWriteFailuresRetry},
    {"readback verification failures and retry", testVerificationFailuresRetry},
    {"interrupt context guards", testInterruptGuards},
    {"destructor always releases buffer", testDestructorCleanup}
  };
  for (const Test &test : tests) {
    test.run();
    CHECK(allocations.empty());
    std::printf("PASS: %s\n", test.name);
  }
  std::printf("EEPROM: %zu test groups, %u checks passed\n",
              sizeof(tests) / sizeof(tests[0]), checks);
  return 0;
}
