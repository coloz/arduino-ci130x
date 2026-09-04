#include "ChipIntelliIR.h"

#include <Arduino.h>
#include <PeripheralManager.h>
#include <limits.h>
#include <string.h>

extern "C" {
#include "FreeRTOS.h"
#include "ci_flash_data_info.h"
#include "crc.h"
#include "flash_manage_outside_port.h"
#include "ir_data.h"
#include "ir_remote_driver.h"
#include "semphr.h"
#include "task.h"
}

namespace {
constexpr uint32_t kSdkReadyTimeoutMs = 10000;
constexpr uint32_t kMutexTimeoutMs = 1000;
constexpr uint32_t kMaximumReceiveTimeoutMs = 60000;
constexpr uint32_t kAirQueueStartTimeoutMs = 10000;
// The official V2.7.14 encoder has database entries that emit two frames with
// vTaskDelay(300) between them. At the configured 500 Hz tick rate that is
// 600 ms, so keep the operation pending until a full second has been quiet.
constexpr uint32_t kAirMultiFrameSettleMs = 1000;
constexpr uint32_t kAirMaximumHardwareTimeMs = 25000;
constexpr uint16_t kMinimumRawDurationUs = 200;
constexpr uint16_t kTrailingReceiveGapUnits = 50000;
constexpr uint32_t kNecHeaderMarkUs = 9000;
constexpr uint32_t kNecHeaderSpaceUs = 4500;
constexpr uint32_t kNecRepeatSpaceUs = 2250;
constexpr uint32_t kNecBitMarkUs = 562;
constexpr uint32_t kNecZeroSpaceUs = 562;
constexpr uint32_t kNecOneSpaceUs = 1688;
constexpr uint32_t kNecFirstRepeatGapUs = 40000;
constexpr uint32_t kNecFollowingRepeatGapUs = 96188;
constexpr uint32_t kNecMinimumTrailingGapUs = 5000;
constexpr uint8_t kMaximumBrand = 35;
constexpr uint8_t kMaximumNecRepeats =
    static_cast<uint8_t>((ChipIntelliIRClass::MaxRawEntries - 67U) / 4U);

alignas(4) uint16_t s_rawBuffer[ChipIntelliIRClass::MaxRawEntries];
stIrPinInfo s_pinInfo = {};
ChipIntelliIRClass *s_hardwareOwner = nullptr;

struct SearchState {
  ChipIntelliIRClass *owner;
  ChipIntelliIRClass::AirSearchCallback callback;
  void *context;
  ChipIntelliIRClass::AirSearchType type;
  bool active;
  bool stopping;
};

SearchState s_search = {nullptr, nullptr, nullptr,
                        ChipIntelliIRClass::AirSearchType::AllBrands, false,
                        false};
ir_search_ctl s_searchControl = {};

struct AirSendTracker {
  ChipIntelliIRClass *owner;
  volatile ChipIntelliIRClass::AirSendStatus status;
  volatile uint32_t changedAtMs;
};

AirSendTracker s_airSend = {nullptr,
                            ChipIntelliIRClass::AirSendStatus::Idle, 0};

class SemaphoreGuard {
public:
  explicit SemaphoreGuard(void *mutex)
      : _mutex(static_cast<SemaphoreHandle_t>(mutex)),
        _locked(_mutex != nullptr &&
                xSemaphoreTake(
                    _mutex,
                    pdMS_TO_TICKS(kMutexTimeoutMs) > 0U
                        ? pdMS_TO_TICKS(kMutexTimeoutMs)
                        : 1U) == pdTRUE) {}

  ~SemaphoreGuard() {
    if (_locked) {
      xSemaphoreGive(_mutex);
    }
  }

  bool locked() const { return _locked; }

private:
  SemaphoreHandle_t _mutex;
  bool _locked;
};

gpio_base_t gpioBase(uint8_t port) {
  static const gpio_base_t bases[] = {PA, PB, PC, PD};
  return bases[port];
}

IRQn_Type gpioIrq(uint8_t port) {
  static const IRQn_Type irqs[] = {PA_IRQn, PB_IRQn, AON_PC_IRQn};
  return irqs[port];
}

pwm_base_t pwmBase(uint8_t channel) {
  static const pwm_base_t bases[] = {PWM0, PWM1, PWM2,
                                     PWM3, PWM4, PWM5};
  return bases[channel];
}

timer_base_t timerBase(uint8_t timer) {
  static const timer_base_t bases[] = {TIMER0, TIMER1, TIMER2, TIMER3};
  return bases[timer];
}

IRQn_Type timerIrq(uint8_t timer) {
  static const IRQn_Type irqs[] = {TIMER0_IRQn, TIMER1_IRQn, TIMER2_IRQn,
                                   TIMER3_IRQn};
  return irqs[timer];
}

PeripheralResource pwmResource(uint8_t channel) {
  return static_cast<PeripheralResource>(
      static_cast<uint8_t>(PeripheralResource::Pwm0) + channel);
}

PeripheralResource timerResource(uint8_t timer) {
  return static_cast<PeripheralResource>(
      static_cast<uint8_t>(PeripheralResource::Timer0) + timer);
}

void releaseConfiguration(uint8_t transmitPin, uint8_t receivePin,
                          uint8_t timer) {
  const uint8_t pins[] = {transmitPin, receivePin};
  const int8_t channel = g_APinDescription[transmitPin].pwmChannel;
  if (channel < 0) {
    PeripheralManager.release(PeripheralOwner::Infrared, pins, 2, nullptr, 0);
    return;
  }
  const PeripheralResource resources[] = {
      pwmResource(static_cast<uint8_t>(channel)), timerResource(timer)};
  PeripheralManager.release(PeripheralOwner::Infrared, pins, 2, resources, 2);
}

bool reserveHardwareOwner(ChipIntelliIRClass *owner) {
  taskENTER_CRITICAL();
  const bool available = s_hardwareOwner == nullptr || s_hardwareOwner == owner;
  if (available) {
    s_hardwareOwner = owner;
  }
  taskEXIT_CRITICAL();
  return available;
}

void releaseHardwareOwner(ChipIntelliIRClass *owner) {
  taskENTER_CRITICAL();
  if (s_hardwareOwner == owner) {
    s_hardwareOwner = nullptr;
  }
  taskEXIT_CRITICAL();
}

bool sdkAndFlashReady(ChipIntelliIRClass::Error &error) {
  if (!chipintelli_sdk_begin()) {
    error = ChipIntelliIRClass::Error::SdkStartFailed;
    return false;
  }

  const uint32_t startedAt = millis();
  chipintelli_sdk_state_t state = chipintelli_sdk_state();
  while (state == CHIPINTELLI_SDK_STARTING) {
    if ((millis() - startedAt) >= kSdkReadyTimeoutMs) {
      error = ChipIntelliIRClass::Error::FlashTimeout;
      return false;
    }
    delay(1);
    state = chipintelli_sdk_state();
  }
  if (state != CHIPINTELLI_SDK_READY) {
    error = ChipIntelliIRClass::Error::SdkStartFailed;
    return false;
  }

  bool flashReady = false;
  is_ci_flash_data_info_inited(&flashReady);
  while (!flashReady) {
    if ((millis() - startedAt) >= kSdkReadyTimeoutMs) {
      error = ChipIntelliIRClass::Error::FlashTimeout;
      return false;
    }
    delay(1);
    is_ci_flash_data_info_inited(&flashReady);
  }
  return true;
}

bool searchActiveFor(const ChipIntelliIRClass *owner) {
  taskENTER_CRITICAL();
  const bool active = s_search.active && s_search.owner == owner;
  taskEXIT_CRITICAL();
  return active;
}

bool isValidAirCommand(ChipIntelliIRClass::AirCommand command) {
  const uint16_t value = static_cast<uint16_t>(command);
  return (value >= 5U && value <= 32U) ||
         (value >= 68U && value <= 75U) || value == 102U ||
         value == 103U || (value >= 200U && value <= 206U) ||
         value == 208U || (value >= 210U && value <= 223U);
}

bool decodeAirCode(uint32_t encoded, uint32_t &decoded) {
  constexpr uint32_t kAirCodeMask = 0x00A5A5A5U;
  const uint32_t packed = encoded ^ kAirCodeMask;
  if ((packed & 0xFF000000U) != 0U) {
    return false;
  }

  const uint32_t tripled = packed >> 8U;
  const uint8_t checksum = static_cast<uint8_t>(packed);
  if (static_cast<uint8_t>(tripled + (tripled >> 8U)) != checksum ||
      (tripled % 3U) != 0U) {
    return false;
  }

  decoded = tripled / 3U;
  // For a model code V2.7.14 stores the database index in bits 15:3 and a
  // candidate number in bits 2:0. Its search callback encodes that complete
  // value, not the bare database index. find_valid_air_code_id() scans the
  // database range [1000, 2199). Brand selectors instead encode the direct
  // pseudo-index [3000, 3035].
  const uint32_t databaseIndex = decoded >> 3U;
  return (databaseIndex >= 1000U && databaseIndex < 2199U) ||
         (decoded >= 3000U && decoded <= 3035U);
}

bool verifyOfficialDatabase(uint32_t address, uint32_t size) {
  if (size != ChipIntelliIRClass::OfficialDatabaseSize ||
      address > UINT32_MAX - size) {
    return false;
  }

  uint8_t *buffer = reinterpret_cast<uint8_t *>(s_rawBuffer);
  constexpr uint32_t kChunkSize = sizeof(s_rawBuffer);
  uint32_t computed = 0;
  uint32_t offset = 0;
  while (offset < size) {
    const uint32_t remaining = size - offset;
    const uint32_t chunk = remaining < kChunkSize ? remaining : kChunkSize;
    if (post_read_flash(reinterpret_cast<char *>(buffer), address + offset,
                        chunk) != RETURN_OK) {
      return false;
    }
    computed = crc32(computed, buffer, chunk);
    offset += chunk;
  }
  return computed == ChipIntelliIRClass::OfficialDatabaseCrc32;
}

void resetAirSendTracker(ChipIntelliIRClass *owner) {
  taskENTER_CRITICAL();
  s_airSend.owner = owner;
  s_airSend.changedAtMs = millis();
  s_airSend.status = ChipIntelliIRClass::AirSendStatus::Idle;
  taskEXIT_CRITICAL();
}

void beginAirSendTracking(ChipIntelliIRClass *owner) {
  taskENTER_CRITICAL();
  s_airSend.owner = owner;
  s_airSend.changedAtMs = millis();
  s_airSend.status = ChipIntelliIRClass::AirSendStatus::Queued;
  taskEXIT_CRITICAL();
}

void cancelAirSendTracking(ChipIntelliIRClass *owner) {
  taskENTER_CRITICAL();
  if (s_airSend.owner == owner) {
    s_airSend.changedAtMs = millis();
    s_airSend.status = ChipIntelliIRClass::AirSendStatus::Idle;
  }
  taskEXIT_CRITICAL();
}

void airDriverCallback(IrRemoteState *state) {
  if (state == nullptr || s_airSend.owner == nullptr) {
    return;
  }

  const ChipIntelliIRClass::AirSendStatus current = s_airSend.status;
  if (current == ChipIntelliIRClass::AirSendStatus::Idle ||
      current == ChipIntelliIRClass::AirSendStatus::Failed) {
    return;
  }

  switch (state->event) {
    case IR_SEND_START:
      s_airSend.changedAtMs = millis();
      s_airSend.status = ChipIntelliIRClass::AirSendStatus::Sending;
      break;
    case IR_SEND_END:
      s_airSend.changedAtMs = millis();
      s_airSend.status = ChipIntelliIRClass::AirSendStatus::Settling;
      break;
    case IR_EVENT_ERR:
    case IR_SEND_DATA_ERR:
      s_airSend.changedAtMs = millis();
      s_airSend.status = ChipIntelliIRClass::AirSendStatus::Failed;
      break;
    default:
      break;
  }
}

ChipIntelliIRClass::AirSendStatus refreshAirSendStatus(
    const ChipIntelliIRClass *owner) {
  taskENTER_CRITICAL();
  if (s_airSend.owner != owner) {
    taskEXIT_CRITICAL();
    return ChipIntelliIRClass::AirSendStatus::Idle;
  }
  const ChipIntelliIRClass::AirSendStatus status = s_airSend.status;
  const uint32_t changedAt = s_airSend.changedAtMs;
  taskEXIT_CRITICAL();

  const uint32_t elapsed = millis() - changedAt;
  ChipIntelliIRClass::AirSendStatus next = status;
  if (status == ChipIntelliIRClass::AirSendStatus::Queued &&
      elapsed >= kAirQueueStartTimeoutMs) {
    next = ChipIntelliIRClass::AirSendStatus::Failed;
  } else if (status == ChipIntelliIRClass::AirSendStatus::Sending &&
             elapsed >= kAirMaximumHardwareTimeMs) {
    next = ChipIntelliIRClass::AirSendStatus::Failed;
  } else if (status == ChipIntelliIRClass::AirSendStatus::Settling &&
             elapsed >= kAirMultiFrameSettleMs) {
    next = ChipIntelliIRClass::AirSendStatus::Idle;
  }

  if (next != status) {
    taskENTER_CRITICAL();
    // Do not overwrite a newer ISR event observed after the snapshot.
    if (s_airSend.owner == owner && s_airSend.status == status &&
        s_airSend.changedAtMs == changedAt) {
      s_airSend.status = next;
    } else {
      next = s_airSend.status;
    }
    taskEXIT_CRITICAL();
  }
  return next;
}

bool airSendActiveFor(const ChipIntelliIRClass *owner) {
  const ChipIntelliIRClass::AirSendStatus status =
      refreshAirSendStatus(owner);
  return status == ChipIntelliIRClass::AirSendStatus::Queued ||
         status == ChipIntelliIRClass::AirSendStatus::Sending ||
         status == ChipIntelliIRClass::AirSendStatus::Settling;
}

int airSearchCallback(eAirSearchCbType callbackType, int airCodeId) {
  ChipIntelliIRClass::AirSearchEvent event =
      ChipIntelliIRClass::AirSearchEvent::CodeSent;
  bool finished = false;
  switch (callbackType) {
    case SEARCH_CB_TYPE_ID:
      break;
    case SEARCH_CB_TYPE_AUTO_OVER_LOOP:
      event = ChipIntelliIRClass::AirSearchEvent::Completed;
      finished = true;
      break;
    case SEARCH_CB_TYPE_ASR_STOP_LOOP:
      event = ChipIntelliIRClass::AirSearchEvent::Stopped;
      finished = true;
      break;
    default:
      return RETURN_ERR;
  }

  taskENTER_CRITICAL();
  ChipIntelliIRClass::AirSearchCallback callback = s_search.callback;
  void *context = s_search.context;
  if (finished) {
    s_search.active = false;
    s_search.owner = nullptr;
    s_search.callback = nullptr;
    s_search.context = nullptr;
    s_search.stopping = false;
  }
  taskEXIT_CRITICAL();

  if (callback != nullptr) {
    callback(event, static_cast<int32_t>(airCodeId), context);
  }
  return RETURN_OK;
}

bool appendNecFrame(uint16_t *buffer, size_t &count, uint8_t byte0,
                    uint8_t byte1, uint8_t command, uint8_t repeats) {
  const size_t required = 67U + static_cast<size_t>(repeats) * 4U;
  if (required > ChipIntelliIRClass::MaxRawEntries) {
    return false;
  }

  const uint8_t bytes[] = {byte0, byte1, command,
                           static_cast<uint8_t>(~command)};
  count = 0;
  buffer[count++] = 4500;  // 9000 us header mark
  buffer[count++] = 2250;  // 4500 us header space
  for (uint8_t value : bytes) {
    for (uint8_t bitIndex = 0; bitIndex < 8; ++bitIndex) {
      buffer[count++] = 281;  // 562 us mark
      buffer[count++] = (value & (1U << bitIndex)) != 0U ? 844 : 281;
    }
  }
  buffer[count++] = 281;

  for (uint8_t repeat = 0; repeat < repeats; ++repeat) {
    // The first repeat follows the full frame after roughly 40 ms. Later NEC
    // repeat frames are spaced at roughly 108 ms from header to header.
    buffer[count++] = repeat == 0 ? 20000 : 48094;
    buffer[count++] = 4500;
    buffer[count++] = 1125;
    buffer[count++] = 281;
  }
  return true;
}

bool matchesNecDuration(uint32_t actual, uint32_t expected,
                        uint8_t tolerancePercent) {
  const uint32_t tolerance =
      (expected * static_cast<uint32_t>(tolerancePercent) + 99U) / 100U;
  const uint32_t minimum = expected > tolerance ? expected - tolerance : 0U;
  return actual >= minimum && actual <= expected + tolerance;
}

template <typename Duration>
bool matchesNecRepeat(const Duration *durationsUs, size_t offset,
                      uint8_t tolerancePercent) {
  return matchesNecDuration(static_cast<uint32_t>(durationsUs[offset]),
                            kNecHeaderMarkUs, tolerancePercent) &&
         matchesNecDuration(static_cast<uint32_t>(durationsUs[offset + 1U]),
                            kNecRepeatSpaceUs, tolerancePercent) &&
         matchesNecDuration(static_cast<uint32_t>(durationsUs[offset + 2U]),
                            kNecBitMarkUs, tolerancePercent);
}

bool isCapturedNecRepeat() {
  if (get_receive_level_count() != 4U) {
    return false;
  }
  const uint16_t *driverBuffer = get_ir_level_code_addr();
  if (driverBuffer == nullptr ||
      driverBuffer[3] != kTrailingReceiveGapUnits) {
    return false;
  }
  return matchesNecDuration(static_cast<uint32_t>(driverBuffer[0]) * 2U,
                            kNecHeaderMarkUs,
                            ChipIntelliIRClass::DefaultNECTolerancePercent) &&
         matchesNecDuration(static_cast<uint32_t>(driverBuffer[1]) * 2U,
                            kNecRepeatSpaceUs,
                            ChipIntelliIRClass::DefaultNECTolerancePercent) &&
         matchesNecDuration(static_cast<uint32_t>(driverBuffer[2]) * 2U,
                            kNecBitMarkUs,
                            ChipIntelliIRClass::DefaultNECTolerancePercent);
}

template <typename Duration>
bool decodeNecDurations(const Duration *durationsUs, size_t count,
                        ChipIntelliIRClass::NECDecodeResult &result,
                        uint8_t tolerancePercent) {
  result.type = ChipIntelliIRClass::NECFrameType::Unknown;
  result.address = 0;
  result.command = 0;
  result.repeatCount = 0;

  if (durationsUs == nullptr || count == 0U ||
      tolerancePercent >
          ChipIntelliIRClass::MaximumNECTolerancePercent) {
    return false;
  }

  // Accept both the canonical three-element repeat and captures/slices that
  // retained one long idle gap on either side.
  if (count == 3U &&
      matchesNecRepeat(durationsUs, 0U, tolerancePercent)) {
    result.type = ChipIntelliIRClass::NECFrameType::Repeat;
    result.repeatCount = 1;
    return true;
  }
  if (count == 4U) {
    const bool trailingGap =
        static_cast<uint32_t>(durationsUs[3]) >= kNecMinimumTrailingGapUs;
    const bool leadingGap =
        static_cast<uint32_t>(durationsUs[0]) >= kNecMinimumTrailingGapUs;
    if ((trailingGap &&
         matchesNecRepeat(durationsUs, 0U, tolerancePercent)) ||
        (leadingGap &&
         matchesNecRepeat(durationsUs, 1U, tolerancePercent))) {
      result.type = ChipIntelliIRClass::NECFrameType::Repeat;
      result.repeatCount = 1;
      return true;
    }
  }

  size_t effectiveCount = count;
  // readRaw() removes the official driver's 100 ms terminator, but callers
  // may also pass captures from another source that retain a final idle gap.
  if (effectiveCount >= 68U && ((effectiveCount - 68U) % 4U) == 0U &&
      static_cast<uint32_t>(durationsUs[effectiveCount - 1U]) >=
          kNecMinimumTrailingGapUs) {
    --effectiveCount;
  }
  if (effectiveCount < 67U || ((effectiveCount - 67U) % 4U) != 0U ||
      !matchesNecDuration(static_cast<uint32_t>(durationsUs[0]),
                          kNecHeaderMarkUs, tolerancePercent) ||
      !matchesNecDuration(static_cast<uint32_t>(durationsUs[1]),
                          kNecHeaderSpaceUs, tolerancePercent)) {
    return false;
  }

  uint8_t bytes[4] = {};
  for (size_t bit = 0; bit < 32U; ++bit) {
    const size_t markIndex = 2U + bit * 2U;
    if (!matchesNecDuration(
            static_cast<uint32_t>(durationsUs[markIndex]), kNecBitMarkUs,
            tolerancePercent)) {
      return false;
    }
    const uint32_t space =
        static_cast<uint32_t>(durationsUs[markIndex + 1U]);
    if (matchesNecDuration(space, kNecZeroSpaceUs, tolerancePercent)) {
      continue;
    }
    if (!matchesNecDuration(space, kNecOneSpaceUs, tolerancePercent)) {
      return false;
    }
    bytes[bit / 8U] |= static_cast<uint8_t>(1U << (bit % 8U));
  }
  if (!matchesNecDuration(static_cast<uint32_t>(durationsUs[66]),
                          kNecBitMarkUs, tolerancePercent) ||
      bytes[3] != static_cast<uint8_t>(~bytes[2])) {
    return false;
  }

  const size_t repeatCount = (effectiveCount - 67U) / 4U;
  if (repeatCount > UINT8_MAX) {
    return false;
  }
  for (size_t repeat = 0; repeat < repeatCount; ++repeat) {
    const size_t offset = 67U + repeat * 4U;
    const uint32_t expectedGap =
        repeat == 0U ? kNecFirstRepeatGapUs : kNecFollowingRepeatGapUs;
    if (!matchesNecDuration(static_cast<uint32_t>(durationsUs[offset]),
                            expectedGap, tolerancePercent) ||
        !matchesNecRepeat(durationsUs, offset + 1U, tolerancePercent)) {
      return false;
    }
  }

  if (bytes[1] == static_cast<uint8_t>(~bytes[0])) {
    result.type = ChipIntelliIRClass::NECFrameType::Standard;
    result.address = bytes[0];
  } else {
    result.type = ChipIntelliIRClass::NECFrameType::Extended;
    result.address = static_cast<uint16_t>(bytes[0]) |
                     (static_cast<uint16_t>(bytes[1]) << 8U);
  }
  result.command = bytes[2];
  result.repeatCount = static_cast<uint8_t>(repeatCount);
  return true;
}
}  // namespace

class ChipIntelliIRFactory {
public:
  static ChipIntelliIRClass instance;
};

ChipIntelliIRClass ChipIntelliIRFactory::instance;
ChipIntelliIRClass &ChipIntelliIR = ChipIntelliIRFactory::instance;

ChipIntelliIRClass::ChipIntelliIRClass()
    : _mode(Mode::None),
      _lastError(Error::None),
      _receiveStatus(ReceiveStatus::Idle),
      _transmitPin(0),
      _receivePin(0),
      _timer(0),
      _resourceId(0),
      _airCode(0),
      _mutex(nullptr),
      _ready(false),
      _airCodeSelected(false),
      _airInitAttempted(false) {}

bool ChipIntelliIRClass::ensureMutex() {
  taskENTER_CRITICAL();
  const bool alreadyCreated = _mutex != nullptr;
  taskEXIT_CRITICAL();
  if (alreadyCreated) {
    return true;
  }

  SemaphoreHandle_t created = xSemaphoreCreateMutex();
  if (created == nullptr) {
    setError(Error::AllocationFailed);
    return false;
  }

  taskENTER_CRITICAL();
  if (_mutex == nullptr) {
    _mutex = created;
    created = nullptr;
  }
  taskEXIT_CRITICAL();
  if (created != nullptr) {
    vSemaphoreDelete(created);
  }
  return true;
}

bool ChipIntelliIRClass::configurationMatches(uint8_t transmitPin,
                                               uint8_t receivePin,
                                               uint8_t timer,
                                               uint16_t resourceId) const {
  return _transmitPin == transmitPin && _receivePin == receivePin &&
         _timer == timer && _resourceId == resourceId;
}

bool ChipIntelliIRClass::beginConfiguration(uint8_t transmitPin,
                                             uint8_t receivePin,
                                             uint8_t timer) {
  if (transmitPin >= NUM_DIGITAL_PINS || receivePin >= NUM_DIGITAL_PINS ||
      transmitPin == receivePin) {
    setError(Error::InvalidPin);
    return false;
  }
  if (timer > 3U) {
    setError(Error::InvalidTimer);
    return false;
  }

  const PinDescription &tx = g_APinDescription[transmitPin];
  const PinDescription &rx = g_APinDescription[receivePin];
  if ((tx.capabilities & PIN_CAP_PWM) == 0U || tx.pwmChannel < 0 ||
      tx.pwmChannel > 5 || (rx.capabilities & PIN_CAP_INTERRUPT) == 0U ||
      tx.port > 3U || rx.port > 2U || tx.bit > 7U || rx.bit > 7U) {
    setError(Error::InvalidPin);
    return false;
  }

  const uint8_t pins[] = {transmitPin, receivePin};
  const PeripheralResource resources[] = {
      pwmResource(static_cast<uint8_t>(tx.pwmChannel)), timerResource(timer)};
  if (!PeripheralManager.claim(PeripheralOwner::Infrared, pins, 2, resources,
                               2)) {
    setError(Error::ResourceBusy);
    return false;
  }

  // GPIO ownership is intentionally replaceable, but the Arduino interrupt
  // dispatcher otherwise retains an old callback for the same edge stream.
  detachInterrupt(receivePin);
  detachInterrupt(transmitPin);

  memset(&s_pinInfo, 0, sizeof(s_pinInfo));
  s_pinInfo.outPin.PinName = static_cast<PinPad_Name>(tx.pad);
  s_pinInfo.outPin.GpioBase = gpioBase(tx.port);
  s_pinInfo.outPin.PinNum = static_cast<gpio_pin_t>(1U << tx.bit);
  s_pinInfo.outPin.PwmFun = static_cast<IOResue_FUNCTION>(tx.pwmMux);
  s_pinInfo.outPin.IoFun = static_cast<IOResue_FUNCTION>(tx.gpioMux);
  s_pinInfo.outPin.PwmBase = pwmBase(static_cast<uint8_t>(tx.pwmChannel));
  s_pinInfo.revPin.PinName = static_cast<PinPad_Name>(rx.pad);
  s_pinInfo.revPin.GpioBase = gpioBase(rx.port);
  s_pinInfo.revPin.PinNum = static_cast<gpio_pin_t>(1U << rx.bit);
  s_pinInfo.revPin.IoFun = static_cast<IOResue_FUNCTION>(rx.gpioMux);
  s_pinInfo.revPin.GpioIRQ = gpioIrq(rx.port);
  s_pinInfo.irTimer.ir_use_timer = timerBase(timer);
  s_pinInfo.irTimer.ir_use_timer_IRQ = timerIrq(timer);

  _transmitPin = transmitPin;
  _receivePin = receivePin;
  _timer = timer;
  return true;
}

bool ChipIntelliIRClass::begin(uint8_t transmitPin, uint8_t receivePin,
                               uint8_t timer) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (_ready) {
    if (_mode == Mode::Raw &&
        configurationMatches(transmitPin, receivePin, timer, 0)) {
      setError(Error::None);
      return true;
    }
    setError(Error::AlreadyBegun);
    return false;
  }
  if (_airInitAttempted || !reserveHardwareOwner(this)) {
    setError(Error::AlreadyBegun);
    return false;
  }
  if (!beginConfiguration(transmitPin, receivePin, timer)) {
    releaseHardwareOwner(this);
    return false;
  }

  if (set_ir_level_code_addr(
          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s_rawBuffer)),
                             sizeof(s_rawBuffer)) != RETURN_OK ||
      ir_setPinInfo(&s_pinInfo) != RETURN_OK) {
    releaseConfiguration(transmitPin, receivePin, timer);
    releaseHardwareOwner(this);
    setError(Error::DriverFailure);
    return false;
  }

  ir_send_init();
  set_odd_even_carry_pwm_wave(1);
  taskENTER_CRITICAL();
  _mode = Mode::Raw;
  _resourceId = 0;
  _receiveStatus = ReceiveStatus::Idle;
  _ready = true;
  taskEXIT_CRITICAL();
  setError(Error::None);
  return true;
}

bool ChipIntelliIRClass::beginAirConditioner(uint8_t transmitPin,
                                             uint8_t receivePin,
                                             uint8_t timer,
                                             uint16_t resourceId) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (_ready) {
    if (_mode == Mode::AirConditioner &&
        configurationMatches(transmitPin, receivePin, timer, resourceId)) {
      setError(Error::None);
      return true;
    }
    setError(Error::AlreadyBegun);
    return false;
  }
  if (_airInitAttempted || !reserveHardwareOwner(this)) {
    setError(Error::AlreadyBegun);
    return false;
  }

  Error startupError = Error::None;
  if (!sdkAndFlashReady(startupError)) {
    releaseHardwareOwner(this);
    setError(startupError);
    return false;
  }

  uint32_t databaseAddress = 0;
  uint32_t databaseSize = 0;
  if (get_userfile_addr(resourceId, &databaseAddress, &databaseSize) != 0U) {
    releaseHardwareOwner(this);
    setError(Error::DatabaseMissing);
    return false;
  }
  if (databaseSize != OfficialDatabaseSize) {
    releaseHardwareOwner(this);
    setError(Error::DatabaseCorrupt);
    return false;
  }
  if (!verifyOfficialDatabase(databaseAddress, databaseSize)) {
    releaseHardwareOwner(this);
    setError(Error::DatabaseCorrupt);
    return false;
  }

  if (!beginConfiguration(transmitPin, receivePin, timer)) {
    releaseHardwareOwner(this);
    return false;
  }

  bool aliasActive = false;
  if (resourceId != 0U) {
    aliasActive = ci_userfile_id_alias_begin(0, resourceId);
    if (!aliasActive) {
      releaseConfiguration(transmitPin, receivePin, timer);
      releaseHardwareOwner(this);
      setError(Error::AliasBusy);
      return false;
    }
  }

  // From here on the vendor library may have allocated persistent objects.
  // It has no deinitializer, so even a failed attempt remains locked out.
  _airInitAttempted = true;
  const int initResult = ir_init(&s_pinInfo);
  if (aliasActive) {
    ci_userfile_id_alias_end();
  }
  if (initResult != RETURN_OK) {
    setError(Error::DriverFailure);
    return false;
  }
  if (ir_hw_init() != RETURN_OK) {
    setError(Error::DriverFailure);
    return false;
  }
  resetAirSendTracker(this);
  registe_ir_remote_callback(airDriverCallback);

  taskENTER_CRITICAL();
  _mode = Mode::AirConditioner;
  _resourceId = resourceId;
  _receiveStatus = ReceiveStatus::Idle;
  _ready = true;
  _airCodeSelected = false;
  taskEXIT_CRITICAL();
  setError(Error::None);
  return true;
}

bool ChipIntelliIRClass::requireMode(Mode expected) {
  if (!_ready) {
    setError(Error::NotReady);
    return false;
  }
  if (_mode != expected) {
    setError(Error::WrongMode);
    return false;
  }
  return true;
}

bool ChipIntelliIRClass::sendRaw(const uint16_t *durationsUs, size_t count) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::Raw)) {
    return false;
  }
  if (durationsUs == nullptr || count == 0 || count > MaxRawEntries) {
    setError(Error::InvalidArgument);
    return false;
  }
  for (size_t index = 0; index < count; ++index) {
    if (durationsUs[index] < kMinimumRawDurationUs) {
      setError(Error::InvalidArgument);
      return false;
    }
  }
  uint16_t *driverBuffer = get_ir_driver_buf();
  if (driverBuffer == nullptr) {
    setError(Error::Busy);
    return false;
  }
  _receiveStatus = ReceiveStatus::Idle;
  set_receive_level_count(0);
  for (size_t index = 0; index < count; ++index) {
    driverBuffer[index] = static_cast<uint16_t>((durationsUs[index] + 1U) / 2U);
  }
  if (send_ir_code_start(static_cast<uint32_t>(count)) != RETURN_OK) {
    setError(Error::DriverFailure);
    return false;
  }
  setError(Error::None);
  return true;
}

bool ChipIntelliIRClass::sendRaw(const uint32_t *durationsUs, size_t count) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::Raw)) {
    return false;
  }
  if (durationsUs == nullptr || count == 0U || count > MaxRawEntries) {
    setError(Error::InvalidArgument);
    return false;
  }
  for (size_t index = 0; index < count; ++index) {
    if (durationsUs[index] < kMinimumRawDurationUs ||
        durationsUs[index] > MaxRawDurationUs) {
      setError(Error::InvalidArgument);
      return false;
    }
  }
  uint16_t *driverBuffer = get_ir_driver_buf();
  if (driverBuffer == nullptr) {
    setError(Error::Busy);
    return false;
  }
  _receiveStatus = ReceiveStatus::Idle;
  set_receive_level_count(0);
  for (size_t index = 0; index < count; ++index) {
    driverBuffer[index] =
        static_cast<uint16_t>((durationsUs[index] + 1U) / 2U);
  }
  if (send_ir_code_start(static_cast<uint32_t>(count)) != RETURN_OK) {
    setError(Error::DriverFailure);
    return false;
  }
  setError(Error::None);
  return true;
}

bool ChipIntelliIRClass::sendNEC(uint8_t address, uint8_t command,
                                 uint8_t repeats) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::Raw)) {
    return false;
  }
  if (repeats > kMaximumNecRepeats) {
    setError(Error::InvalidArgument);
    return false;
  }
  uint16_t *driverBuffer = get_ir_driver_buf();
  if (driverBuffer == nullptr) {
    setError(Error::Busy);
    return false;
  }
  _receiveStatus = ReceiveStatus::Idle;
  set_receive_level_count(0);
  size_t count = 0;
  if (!appendNecFrame(driverBuffer, count, address,
                      static_cast<uint8_t>(~address), command, repeats)) {
    setError(Error::InvalidArgument);
    return false;
  }
  if (send_ir_code_start(static_cast<uint32_t>(count)) != RETURN_OK) {
    setError(Error::DriverFailure);
    return false;
  }
  setError(Error::None);
  return true;
}

bool ChipIntelliIRClass::sendExtendedNEC(uint16_t address, uint8_t command,
                                         uint8_t repeats) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::Raw)) {
    return false;
  }
  if (repeats > kMaximumNecRepeats) {
    setError(Error::InvalidArgument);
    return false;
  }
  uint16_t *driverBuffer = get_ir_driver_buf();
  if (driverBuffer == nullptr) {
    setError(Error::Busy);
    return false;
  }
  _receiveStatus = ReceiveStatus::Idle;
  set_receive_level_count(0);
  size_t count = 0;
  if (!appendNecFrame(driverBuffer, count, static_cast<uint8_t>(address),
                      static_cast<uint8_t>(address >> 8), command, repeats)) {
    setError(Error::InvalidArgument);
    return false;
  }
  if (send_ir_code_start(static_cast<uint32_t>(count)) != RETURN_OK) {
    setError(Error::DriverFailure);
    return false;
  }
  setError(Error::None);
  return true;
}

bool ChipIntelliIRClass::startReceive(uint32_t timeoutMs) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::Raw)) {
    return false;
  }
  if (timeoutMs == 0U || timeoutMs > kMaximumReceiveTimeoutMs) {
    setError(Error::InvalidArgument);
    return false;
  }
  if (check_ir_busy_state() == RETURN_OK) {
    setError(Error::Busy);
    return false;
  }
  set_receive_level_count(0);
  ir_receive_start(static_cast<int>(timeoutMs));
  if (check_ir_busy_state() != RETURN_OK) {
    _receiveStatus = ReceiveStatus::Error;
    setError(Error::DriverFailure);
    return false;
  }
  _receiveStatus = ReceiveStatus::Receiving;
  setError(Error::None);
  return true;
}

bool ChipIntelliIRClass::stopReceive() {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::Raw)) {
    return false;
  }
  if (_receiveStatus == ReceiveStatus::Receiving &&
      check_ir_busy_state() == RETURN_OK) {
    timer_stop(s_pinInfo.irTimer.ir_use_timer);
    ir_receive_end();
  }
  set_receive_level_count(0);
  _receiveStatus = ReceiveStatus::Idle;
  setError(Error::None);
  return true;
}

ChipIntelliIRClass::ReceiveStatus ChipIntelliIRClass::pollReceiveStatus() {
  if (_receiveStatus != ReceiveStatus::Receiving ||
      check_ir_busy_state() == RETURN_OK) {
    return _receiveStatus;
  }
  if (check_ir_receive() == RETURN_OK) {
    _receiveStatus = ReceiveStatus::Ready;
  } else if (isCapturedNecRepeat()) {
    // The vendor driver labels every capture shorter than 16 entries as an
    // error. A standalone NEC repeat is valid but has only three waveform
    // entries plus the driver's terminating gap, so recover it explicitly.
    _receiveStatus = ReceiveStatus::Ready;
  } else if (get_receive_level_count() == 0U) {
    _receiveStatus = ReceiveStatus::Timeout;
  } else {
    _receiveStatus = ReceiveStatus::Error;
  }
  return _receiveStatus;
}

ChipIntelliIRClass::ReceiveStatus ChipIntelliIRClass::receiveStatus() {
  if (!ensureMutex()) {
    return ReceiveStatus::Error;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return ReceiveStatus::Error;
  }
  if (!requireMode(Mode::Raw)) {
    return ReceiveStatus::Error;
  }
  return pollReceiveStatus();
}

bool ChipIntelliIRClass::readRaw(uint16_t *durationsUs, size_t capacity,
                                 size_t &count) {
  count = 0;
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::Raw)) {
    return false;
  }
  if (pollReceiveStatus() != ReceiveStatus::Ready) {
    setError(Error::NotReady);
    return false;
  }

  uint32_t received = get_receive_level_count();
  uint16_t *driverBuffer = get_ir_level_code_addr();
  if (driverBuffer == nullptr || received == 0U || received > MaxRawEntries) {
    _receiveStatus = ReceiveStatus::Error;
    setError(Error::DriverFailure);
    return false;
  }
  if (driverBuffer[received - 1U] == kTrailingReceiveGapUnits) {
    --received;
  }
  count = static_cast<size_t>(received);
  if (durationsUs == nullptr || capacity < count) {
    setError(Error::BufferTooSmall);
    return false;
  }
  for (size_t index = 0; index < count; ++index) {
    const uint32_t duration = static_cast<uint32_t>(driverBuffer[index]) * 2U;
    if (duration > UINT16_MAX) {
      setError(Error::InvalidArgument);
      return false;
    }
    durationsUs[index] = static_cast<uint16_t>(duration);
  }
  set_receive_level_count(0);
  _receiveStatus = ReceiveStatus::Idle;
  setError(Error::None);
  return true;
}

bool ChipIntelliIRClass::readRaw(uint32_t *durationsUs, size_t capacity,
                                 size_t &count) {
  count = 0;
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::Raw)) {
    return false;
  }
  if (pollReceiveStatus() != ReceiveStatus::Ready) {
    setError(Error::NotReady);
    return false;
  }

  uint32_t received = get_receive_level_count();
  uint16_t *driverBuffer = get_ir_level_code_addr();
  if (driverBuffer == nullptr || received == 0U || received > MaxRawEntries) {
    _receiveStatus = ReceiveStatus::Error;
    setError(Error::DriverFailure);
    return false;
  }
  if (driverBuffer[received - 1U] == kTrailingReceiveGapUnits) {
    --received;
  }
  count = static_cast<size_t>(received);
  if (durationsUs == nullptr || capacity < count) {
    setError(Error::BufferTooSmall);
    return false;
  }
  for (size_t index = 0; index < count; ++index) {
    durationsUs[index] = static_cast<uint32_t>(driverBuffer[index]) * 2U;
  }
  set_receive_level_count(0);
  _receiveStatus = ReceiveStatus::Idle;
  setError(Error::None);
  return true;
}

bool ChipIntelliIRClass::decodeNEC(const uint16_t *durationsUs, size_t count,
                                   NECDecodeResult &result,
                                   uint8_t tolerancePercent) {
  return decodeNecDurations(durationsUs, count, result, tolerancePercent);
}

bool ChipIntelliIRClass::decodeNEC(const uint32_t *durationsUs, size_t count,
                                   NECDecodeResult &result,
                                   uint8_t tolerancePercent) {
  return decodeNecDurations(durationsUs, count, result, tolerancePercent);
}

bool ChipIntelliIRClass::isBusy() const {
  taskENTER_CRITICAL();
  const bool ready = _ready;
  void *mutex = _mutex;
  taskEXIT_CRITICAL();
  if (!ready || mutex == nullptr) {
    return false;
  }
  SemaphoreGuard guard(mutex);
  if (!guard.locked()) {
    return true;
  }
  return check_ir_busy_state() == RETURN_OK || searchActiveFor(this) ||
         airSendActiveFor(this);
}

bool ChipIntelliIRClass::waitUntilIdle(uint32_t timeoutMs) {
  if (!ensureMutex()) {
    return false;
  }
  {
    SemaphoreGuard guard(_mutex);
    if (!guard.locked()) {
      setError(Error::MutexTimeout);
      return false;
    }
    if (!_ready) {
      setError(Error::NotReady);
      return false;
    }
  }

  const uint32_t startedAt = millis();
  while (true) {
    const AirSendStatus airStatus = airSendStatus();
    if (airStatus == AirSendStatus::Failed) {
      setError(Error::DriverFailure);
      return false;
    }
    if (!isBusy()) {
      setError(Error::None);
      return true;
    }
    if ((millis() - startedAt) >= timeoutMs) {
      setError(Error::OperationTimeout);
      return false;
    }
    delay(1);
  }
}

bool ChipIntelliIRClass::selectAirBrand(AirBrand brand) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::AirConditioner)) {
    return false;
  }
  const uint8_t value = static_cast<uint8_t>(brand);
  if (value > kMaximumBrand) {
    setError(Error::InvalidArgument);
    return false;
  }
  if (searchActiveFor(this) || airSendActiveFor(this) ||
      check_ir_busy_state() == RETURN_OK) {
    setError(Error::Busy);
    return false;
  }
  const int code = get_airc_brand_id(static_cast<eAirBrand>(value));
  uint32_t decoded = 0;
  if (code == RETURN_ERR ||
      !decodeAirCode(static_cast<uint32_t>(code), decoded) ||
      decoded != 3000U + value) {
    setError(Error::DriverFailure);
    return false;
  }
  taskENTER_CRITICAL();
  _airCode = static_cast<uint32_t>(code);
  _airCodeSelected = true;
  taskEXIT_CRITICAL();
  set_g_air_code_index(_airCode);
  setError(Error::None);
  return true;
}

bool ChipIntelliIRClass::selectAirCode(uint32_t codeId) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::AirConditioner)) {
    return false;
  }
  uint32_t decoded = 0;
  if (!decodeAirCode(codeId, decoded)) {
    setError(Error::InvalidArgument);
    return false;
  }
  if (searchActiveFor(this) || airSendActiveFor(this) ||
      check_ir_busy_state() == RETURN_OK) {
    setError(Error::Busy);
    return false;
  }
  taskENTER_CRITICAL();
  _airCode = codeId;
  _airCodeSelected = true;
  taskEXIT_CRITICAL();
  set_g_air_code_index(codeId);
  setError(Error::None);
  return true;
}

uint32_t ChipIntelliIRClass::airCode() const {
  taskENTER_CRITICAL();
  const uint32_t code = _airCode;
  taskEXIT_CRITICAL();
  return code;
}

ChipIntelliIRClass::AirSendStatus ChipIntelliIRClass::airSendStatus() const {
  taskENTER_CRITICAL();
  const bool airReady = _ready && _mode == Mode::AirConditioner;
  taskEXIT_CRITICAL();
  return airReady ? refreshAirSendStatus(this) : AirSendStatus::Idle;
}

bool ChipIntelliIRClass::sendAirUnlocked(AirCommand command) {
  if (!isValidAirCommand(command)) {
    setError(Error::InvalidArgument);
    return false;
  }
  if (!_airCodeSelected) {
    setError(Error::AirCodeNotSelected);
    return false;
  }
  if (searchActiveFor(this) || airSendActiveFor(this) ||
      check_ir_busy_state() == RETURN_OK) {
    setError(Error::Busy);
    return false;
  }
  beginAirSendTracking(this);
  if (ir_data_Air_Send_Ctl(static_cast<int>(_airCode),
                           static_cast<int>(command)) == RETURN_ERR) {
    cancelAirSendTracking(this);
    setError(Error::DriverFailure);
    return false;
  }
  setError(Error::None);
  return true;
}

bool ChipIntelliIRClass::sendAir(AirCommand command) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::AirConditioner)) {
    return false;
  }
  return sendAirUnlocked(command);
}

bool ChipIntelliIRClass::setTemperature(uint8_t celsius) {
  if (celsius < 16U || celsius > 30U) {
    setError(Error::InvalidArgument);
    return false;
  }
  const uint16_t command = celsius <= 18U
                               ? static_cast<uint16_t>(30U + celsius - 16U)
                               : static_cast<uint16_t>(13U + celsius - 19U);
  return sendAir(static_cast<AirCommand>(command));
}

bool ChipIntelliIRClass::power(bool on) {
  return sendAir(on ? AirCommand::PowerOn : AirCommand::PowerOff);
}

bool ChipIntelliIRClass::startAirSearch(AirSearchType type,
                                        AirSearchCallback callback,
                                        void *context, uint8_t sendCount,
                                        uint32_t intervalMs) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::AirConditioner)) {
    return false;
  }
  if (callback == nullptr || sendCount < 3U || intervalMs < 3000U ||
      intervalMs > static_cast<uint32_t>(INT_MAX)) {
    setError(Error::InvalidArgument);
    return false;
  }
  if (type != AirSearchType::AllBrands &&
      type != AirSearchType::CurrentBrandModels) {
    setError(Error::InvalidArgument);
    return false;
  }
  if (type == AirSearchType::CurrentBrandModels && !_airCodeSelected) {
    setError(Error::AirCodeNotSelected);
    return false;
  }
  if (searchActiveFor(this) || airSendActiveFor(this) ||
      check_ir_busy_state() == RETURN_OK) {
    setError(Error::Busy);
    return false;
  }

  taskENTER_CRITICAL();
  if (s_search.active) {
    taskEXIT_CRITICAL();
    setError(Error::Busy);
    return false;
  }
  s_search.owner = this;
  s_search.callback = callback;
  s_search.context = context;
  s_search.type = type;
  s_search.active = true;
  s_search.stopping = false;
  taskEXIT_CRITICAL();

  s_searchControl.send_cnt = sendCount;
  s_searchControl.timeout_ms = static_cast<int>(intervalMs);
  s_searchControl.ir_search_send_callback = airSearchCallback;
  const ir_ctrl_cmd_t command =
      type == AirSearchType::AllBrands ? IR_SERCH_AIR_BRAND
                                       : IR_SERCH_AIR_INDEX;
  if (ir_data_search_ctl(command, &s_searchControl) == RETURN_ERR) {
    taskENTER_CRITICAL();
    s_search = {nullptr, nullptr, nullptr, AirSearchType::AllBrands, false,
                false};
    taskEXIT_CRITICAL();
    setError(Error::DriverFailure);
    return false;
  }
  setError(Error::None);
  return true;
}

bool ChipIntelliIRClass::stopAirSearch() {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked()) {
    setError(Error::MutexTimeout);
    return false;
  }
  if (!requireMode(Mode::AirConditioner)) {
    return false;
  }
  taskENTER_CRITICAL();
  const bool active = s_search.active && s_search.owner == this;
  const AirSearchType type = s_search.type;
  const bool stopping = s_search.stopping;
  taskEXIT_CRITICAL();
  if (!active) {
    setError(Error::NotReady);
    return false;
  }
  if (stopping) {
    setError(Error::None);
    return true;
  }
  const ir_ctrl_cmd_t command =
      type == AirSearchType::AllBrands ? IR_STOP_SERCH_AIR_BRAND
                                       : IR_STOP_SERCH_AIR_INDEX;
  if (ir_data_search_ctl(command, nullptr) == RETURN_ERR) {
    setError(Error::DriverFailure);
    return false;
  }
  taskENTER_CRITICAL();
  if (s_search.active && s_search.owner == this) {
    s_search.stopping = true;
  }
  taskEXIT_CRITICAL();
  setError(Error::None);
  return true;
}

ChipIntelliIRClass::Mode ChipIntelliIRClass::mode() const {
  taskENTER_CRITICAL();
  const Mode current = _mode;
  taskEXIT_CRITICAL();
  return current;
}

ChipIntelliIRClass::Error ChipIntelliIRClass::lastError() const {
  taskENTER_CRITICAL();
  const Error error = _lastError;
  taskEXIT_CRITICAL();
  return error;
}

void ChipIntelliIRClass::setError(Error error) {
  taskENTER_CRITICAL();
  _lastError = error;
  taskEXIT_CRITICAL();
}

const char *ChipIntelliIRClass::errorString() const {
  return errorString(lastError());
}

const char *ChipIntelliIRClass::errorString(Error error) {
  switch (error) {
    case Error::None: return "no error";
    case Error::AlreadyBegun: return "infrared hardware is already initialized";
    case Error::WrongMode: return "operation is unavailable in the active mode";
    case Error::InvalidPin: return "invalid infrared pin selection";
    case Error::InvalidTimer: return "timer must be between 0 and 3";
    case Error::InvalidArgument: return "invalid argument";
    case Error::ResourceBusy: return "pin, PWM, or timer resource is busy";
    case Error::Busy: return "infrared driver is busy";
    case Error::NotReady: return "requested infrared result is not ready";
    case Error::BufferTooSmall: return "destination buffer is too small";
    case Error::DriverFailure: return "official infrared driver rejected the operation";
    case Error::SdkStartFailed: return "ChipIntelli SDK startup failed";
    case Error::FlashTimeout: return "timed out waiting for SDK flash resources";
    case Error::DatabaseMissing: return "compatible air-conditioner database is missing";
    case Error::DatabaseCorrupt: return "air-conditioner database failed integrity validation";
    case Error::AliasBusy: return "user-file compatibility alias is busy";
    case Error::AllocationFailed: return "unable to allocate the infrared mutex";
    case Error::MutexTimeout: return "timed out waiting for the infrared mutex";
    case Error::AirCodeNotSelected: return "select an air-conditioner code first";
    case Error::OperationTimeout: return "timed out waiting for the infrared operation";
  }
  return "unknown infrared error";
}
