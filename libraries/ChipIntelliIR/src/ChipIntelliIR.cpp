#include "ChipIntelliIR.h"

#include <Arduino.h>
#include <PeripheralManager.h>
#include <limits.h>
#include <string.h>

extern "C" {
#include "FreeRTOS.h"
#include "ci_flash_data_info.h"
#include "ir_data.h"
#include "ir_remote_driver.h"
#include "semphr.h"
#include "task.h"
}

namespace {
constexpr uint32_t kSdkReadyTimeoutMs = 10000;
constexpr uint32_t kMaximumReceiveTimeoutMs = 60000;
constexpr uint16_t kMinimumRawDurationUs = 200;
constexpr uint16_t kTrailingReceiveGapUnits = 50000;
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

class SemaphoreGuard {
public:
  explicit SemaphoreGuard(void *mutex)
      : _mutex(static_cast<SemaphoreHandle_t>(mutex)),
        _locked(_mutex != nullptr &&
                xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {}

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
    setError(Error::AllocationFailed);
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
    setError(Error::AllocationFailed);
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
  (void)databaseAddress;
  if (databaseSize != OfficialDatabaseSize) {
    releaseHardwareOwner(this);
    setError(Error::DatabaseMissing);
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
  if (!guard.locked() || !requireMode(Mode::Raw)) {
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

bool ChipIntelliIRClass::sendNEC(uint8_t address, uint8_t command,
                                 uint8_t repeats) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked() || !requireMode(Mode::Raw)) {
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
  if (!guard.locked() || !requireMode(Mode::Raw)) {
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
  if (!guard.locked() || !requireMode(Mode::Raw)) {
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
  if (!guard.locked() || !requireMode(Mode::Raw)) {
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
  if (!guard.locked() || !requireMode(Mode::Raw)) {
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
  if (!guard.locked() || !requireMode(Mode::Raw)) {
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
  return check_ir_busy_state() == RETURN_OK || searchActiveFor(this);
}

bool ChipIntelliIRClass::selectAirBrand(AirBrand brand) {
  if (!ensureMutex()) {
    return false;
  }
  SemaphoreGuard guard(_mutex);
  if (!guard.locked() || !requireMode(Mode::AirConditioner)) {
    return false;
  }
  const uint8_t value = static_cast<uint8_t>(brand);
  if (value > kMaximumBrand) {
    setError(Error::InvalidArgument);
    return false;
  }
  if (searchActiveFor(this) || check_ir_busy_state() == RETURN_OK) {
    setError(Error::Busy);
    return false;
  }
  const int code = get_airc_brand_id(static_cast<eAirBrand>(value));
  if (code == RETURN_ERR) {
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
  if (!guard.locked() || !requireMode(Mode::AirConditioner)) {
    return false;
  }
  if (searchActiveFor(this) || check_ir_busy_state() == RETURN_OK) {
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

bool ChipIntelliIRClass::sendAirUnlocked(AirCommand command) {
  if (!_airCodeSelected) {
    setError(Error::AirCodeNotSelected);
    return false;
  }
  if (searchActiveFor(this) || check_ir_busy_state() == RETURN_OK) {
    setError(Error::Busy);
    return false;
  }
  if (ir_data_Air_Send_Ctl(static_cast<int>(_airCode),
                           static_cast<int>(command)) == RETURN_ERR) {
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
  if (!guard.locked() || !requireMode(Mode::AirConditioner)) {
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
  if (!guard.locked() || !requireMode(Mode::AirConditioner)) {
    return false;
  }
  if (callback == nullptr || sendCount < 3U || intervalMs < 3000U ||
      intervalMs > static_cast<uint32_t>(INT_MAX)) {
    setError(Error::InvalidArgument);
    return false;
  }
  if (type == AirSearchType::CurrentBrandModels && !_airCodeSelected) {
    setError(Error::AirCodeNotSelected);
    return false;
  }
  if (searchActiveFor(this) || check_ir_busy_state() == RETURN_OK) {
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
  if (!guard.locked() || !requireMode(Mode::AirConditioner)) {
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
    case Error::AliasBusy: return "user-file compatibility alias is busy";
    case Error::AllocationFailed: return "unable to allocate the infrared mutex";
    case Error::AirCodeNotSelected: return "select an air-conditioner code first";
  }
  return "unknown infrared error";
}
