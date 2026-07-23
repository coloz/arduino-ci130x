#include "ChipIntelliTimer.h"

#include <Arduino.h>
#include <PeripheralManager.h>
#include <limits.h>

extern "C" {
#include "FreeRTOS.h"
#include "ci130x_core_eclic.h"
#include "ci130x_core_misc.h"
#include "ci130x_scu.h"
#include "ci130x_timer.h"
#include "platform_config.h"
#include "task.h"
}

namespace {
constexpr uint8_t kHardwareTimerCount = 4;
#if defined(USE_BLE) && USE_BLE
// The official BLE stack unconditionally resets TIMER3, installs its own
// interrupt vector, and uses it as the RF time base. It does not participate in
// PeripheralManager, so keep TIMER3 out of Arduino allocation in BLE builds.
constexpr uint8_t kAvailableTimerCount = 3;
#else
constexpr uint8_t kAvailableTimerCount = kHardwareTimerCount;
#endif
constexpr uint8_t kInterruptLevel = 3;
constexpr uint8_t kInterruptPriority = 0;

const timer_base_t kTimerBases[kHardwareTimerCount] = {
    TIMER0, TIMER1, TIMER2, TIMER3};
const IRQn_Type kTimerIrqs[kHardwareTimerCount] = {
    TIMER0_IRQn, TIMER1_IRQn, TIMER2_IRQn, TIMER3_IRQn};
const uint32_t kTimerGates[kHardwareTimerCount] = {
    HAL_TIMER0_BASE, HAL_TIMER1_BASE, HAL_TIMER2_BASE, HAL_TIMER3_BASE};

PeripheralResource timerResource(uint8_t timerNumber) {
  return static_cast<PeripheralResource>(
      static_cast<uint8_t>(PeripheralResource::Timer0) + timerNumber);
}

bool calculateCount(uint32_t periodMicros, timer_clock_div_t &clockDiv,
                    uint32_t &count) {
  const uint32_t clock = get_apb_clk();
  if (clock == 0U || periodMicros == 0U) {
    return false;
  }

  struct Divider {
    uint8_t value;
    timer_clock_div_t setting;
  };
  static const Divider dividers[] = {
      {1, timer_clk_div_0}, {2, timer_clk_div_2},
      {4, timer_clk_div_4}, {16, timer_clk_div_16}};

  const uint64_t numerator =
      static_cast<uint64_t>(clock) * periodMicros;
  for (const Divider &divider : dividers) {
    const uint64_t denominator =
        static_cast<uint64_t>(1000000U) * divider.value;
    const uint64_t candidate =
        (numerator + denominator / 2U) / denominator;
    if (candidate >= 1U && candidate <= UINT32_MAX) {
      clockDiv = divider.setting;
      count = static_cast<uint32_t>(candidate);
      return true;
    }
  }
  return false;
}
}  // namespace

ChipIntelliTimer *ChipIntelliTimer::s_owners[kHardwareTimerCount] = {};

ChipIntelliTimer::ChipIntelliTimer()
    : _callback(nullptr),
      _callbackWithArg(nullptr),
      _argument(nullptr),
      _periodMicros(0),
      _timerNumber(Automatic),
      _repeat(false),
      _begun(false),
      _operationInProgress(false),
      _running(false),
      _lastError(Error::None) {}

ChipIntelliTimer::~ChipIntelliTimer() { end(); }

bool ChipIntelliTimer::begin(uint32_t periodMicros, Callback callback,
                            bool repeat) {
  return beginInternal(Automatic, periodMicros, callback, nullptr, nullptr,
                       repeat);
}

bool ChipIntelliTimer::begin(uint32_t periodMicros,
                            CallbackWithArg callback, void *argument,
                            bool repeat) {
  return beginInternal(Automatic, periodMicros, nullptr, callback, argument,
                       repeat);
}

bool ChipIntelliTimer::begin(uint8_t timerNumber, uint32_t periodMicros,
                            Callback callback, bool repeat) {
  return beginInternal(timerNumber, periodMicros, callback, nullptr, nullptr,
                       repeat);
}

bool ChipIntelliTimer::begin(uint8_t timerNumber, uint32_t periodMicros,
                            CallbackWithArg callback, void *argument,
                            bool repeat) {
  return beginInternal(timerNumber, periodMicros, nullptr, callback, argument,
                       repeat);
}

bool ChipIntelliTimer::beginInternal(uint8_t timerNumber,
                                     uint32_t periodMicros, Callback callback,
                                     CallbackWithArg callbackWithArg,
                                     void *argument, bool repeat) {
  if (!beginOperation()) {
    return false;
  }
  if (_begun) {
    _lastError = Error::AlreadyBegun;
    finishOperation();
    return false;
  }
  if (timerNumber != Automatic && timerNumber >= kHardwareTimerCount) {
    _lastError = Error::InvalidTimer;
    finishOperation();
    return false;
  }
  if (timerNumber != Automatic && timerNumber >= kAvailableTimerCount) {
    _lastError = Error::ResourceBusy;
    finishOperation();
    return false;
  }
  if (callback == nullptr && callbackWithArg == nullptr) {
    _lastError = Error::InvalidCallback;
    finishOperation();
    return false;
  }
  if (periodMicros == 0U) {
    _lastError = Error::InvalidPeriod;
    finishOperation();
    return false;
  }

  timer_clock_div_t ignoredDiv;
  uint32_t ignoredCount;
  if (!calculateCount(periodMicros, ignoredDiv, ignoredCount)) {
    _lastError = get_apb_clk() == 0U ? Error::ClockUnavailable
                                    : Error::InvalidPeriod;
    finishOperation();
    return false;
  }

  bool acquired = false;
  if (timerNumber == Automatic) {
    for (uint8_t candidate = 0; candidate < kAvailableTimerCount; ++candidate) {
      if (acquire(candidate)) {
        acquired = true;
        break;
      }
    }
  } else {
    acquired = acquire(timerNumber);
  }
  if (!acquired) {
    _lastError = Error::ResourceBusy;
    finishOperation();
    return false;
  }

  _callback = callback;
  _callbackWithArg = callbackWithArg;
  _argument = argument;
  _repeat = repeat;
  _begun = true;

  if (!configure(periodMicros)) {
    endInternal();
    _lastError = get_apb_clk() == 0U ? Error::ClockUnavailable
                                    : Error::InvalidPeriod;
    finishOperation();
    return false;
  }

  const IRQn_Type irq = kTimerIrqs[_timerNumber];
  static void (*const handlers[kHardwareTimerCount])() = {
      handleTimer0, handleTimer1, handleTimer2, handleTimer3};
  __eclic_irq_set_vector(
      irq, static_cast<int32_t>(
               reinterpret_cast<uintptr_t>(handlers[_timerNumber])));
  eclic_irq_set_priority(irq, kInterruptLevel, kInterruptPriority);
  eclic_clear_pending(irq);
  eclic_irq_enable(irq);
  timer_start(kTimerBases[_timerNumber]);
  _running = true;
  _lastError = Error::None;
  finishOperation();
  return true;
}

bool ChipIntelliTimer::acquire(uint8_t timerNumber) {
  taskENTER_CRITICAL();
  if (s_owners[timerNumber] != nullptr) {
    taskEXIT_CRITICAL();
    return false;
  }
  s_owners[timerNumber] = this;
  taskEXIT_CRITICAL();

  const PeripheralResource resource = timerResource(timerNumber);
  if (!PeripheralManager.claimResource(PeripheralOwner::Timer, resource)) {
    taskENTER_CRITICAL();
    if (s_owners[timerNumber] == this) {
      s_owners[timerNumber] = nullptr;
    }
    taskEXIT_CRITICAL();
    return false;
  }

  _timerNumber = timerNumber;
  return true;
}

bool ChipIntelliTimer::configure(uint32_t periodMicros) {
  timer_clock_div_t clockDiv;
  uint32_t count;
  if (!calculateCount(periodMicros, clockDiv, count)) {
    return false;
  }

  const timer_base_t base = kTimerBases[_timerNumber];
  scu_set_device_gate(kTimerGates[_timerNumber], ENABLE);
  scu_set_device_reset(kTimerGates[_timerNumber]);
  scu_set_device_reset_release(kTimerGates[_timerNumber]);

  timer_init_t init = {};
  init.mode = _repeat ? timer_count_mode_auto : timer_count_mode_single;
  init.div = clockDiv;
  init.width = timer_iqr_width_f;
  init.count = count;
  timer_init(base, init);
  timer_clear_irq(base);
  _periodMicros = periodMicros;
  return true;
}

bool ChipIntelliTimer::beginOperation() {
  if (check_curr_trap() != 0) {
    _lastError = Error::InterruptContext;
    return false;
  }
  taskENTER_CRITICAL();
  if (_operationInProgress) {
    taskEXIT_CRITICAL();
    _lastError = Error::Busy;
    return false;
  }
  _operationInProgress = true;
  taskEXIT_CRITICAL();
  return true;
}

void ChipIntelliTimer::finishOperation() {
  taskENTER_CRITICAL();
  _operationInProgress = false;
  taskEXIT_CRITICAL();
}

void ChipIntelliTimer::endInternal() {
  if (!_begun || _timerNumber >= kHardwareTimerCount) {
    return;
  }

  const uint8_t timerNumber = _timerNumber;
  const timer_base_t base = kTimerBases[timerNumber];
  const IRQn_Type irq = kTimerIrqs[timerNumber];
  eclic_irq_disable(irq);
  timer_stop(base);
  timer_clear_irq(base);
  eclic_clear_pending(irq);
  _running = false;
  scu_set_device_gate(kTimerGates[timerNumber], DISABLE);
  PeripheralManager.releaseResource(PeripheralOwner::Timer,
                                    timerResource(timerNumber));

  taskENTER_CRITICAL();
  if (s_owners[timerNumber] == this) {
    s_owners[timerNumber] = nullptr;
  }
  taskEXIT_CRITICAL();

  _callback = nullptr;
  _callbackWithArg = nullptr;
  _argument = nullptr;
  _periodMicros = 0;
  _timerNumber = Automatic;
  _repeat = false;
  _begun = false;
  _lastError = Error::None;
}

void ChipIntelliTimer::end() {
  if (!beginOperation()) {
    return;
  }
  endInternal();
  finishOperation();
}

bool ChipIntelliTimer::start() {
  if (!beginOperation()) {
    return false;
  }
  if (!_begun) {
    _lastError = Error::NotBegun;
    finishOperation();
    return false;
  }
  const timer_base_t base = kTimerBases[_timerNumber];
  timer_clear_irq(base);
  eclic_clear_pending(kTimerIrqs[_timerNumber]);
  timer_start(base);
  _running = true;
  _lastError = Error::None;
  finishOperation();
  return true;
}

void ChipIntelliTimer::stop() {
  if (!beginOperation()) {
    return;
  }
  if (!_begun) {
    _lastError = Error::NotBegun;
    finishOperation();
    return;
  }
  timer_stop(kTimerBases[_timerNumber]);
  timer_clear_irq(kTimerBases[_timerNumber]);
  eclic_clear_pending(kTimerIrqs[_timerNumber]);
  _running = false;
  _lastError = Error::None;
  finishOperation();
}

bool ChipIntelliTimer::restart() { return start(); }

bool ChipIntelliTimer::setPeriod(uint32_t periodMicros) {
  if (!beginOperation()) {
    return false;
  }
  if (!_begun) {
    _lastError = Error::NotBegun;
    finishOperation();
    return false;
  }
  if (periodMicros == 0U) {
    _lastError = Error::InvalidPeriod;
    finishOperation();
    return false;
  }

  timer_clock_div_t ignoredDiv;
  uint32_t ignoredCount;
  if (!calculateCount(periodMicros, ignoredDiv, ignoredCount)) {
    _lastError = get_apb_clk() == 0U ? Error::ClockUnavailable
                                    : Error::InvalidPeriod;
    finishOperation();
    return false;
  }

  const bool wasRunning = _running;
  const IRQn_Type irq = kTimerIrqs[_timerNumber];
  eclic_irq_disable(irq);
  timer_stop(kTimerBases[_timerNumber]);
  _running = false;
  if (!configure(periodMicros)) {
    eclic_irq_enable(irq);
    _lastError = Error::InvalidPeriod;
    finishOperation();
    return false;
  }
  eclic_clear_pending(irq);
  eclic_irq_enable(irq);
  if (wasRunning) {
    timer_start(kTimerBases[_timerNumber]);
    _running = true;
  }
  _lastError = Error::None;
  finishOperation();
  return true;
}

bool ChipIntelliTimer::begun() const { return _begun; }

bool ChipIntelliTimer::running() const { return _running; }

bool ChipIntelliTimer::repeating() const { return _repeat; }

int8_t ChipIntelliTimer::timerNumber() const {
  return _begun ? static_cast<int8_t>(_timerNumber) : -1;
}

uint32_t ChipIntelliTimer::period() const { return _periodMicros; }

ChipIntelliTimer::Error ChipIntelliTimer::lastError() const {
  return _lastError;
}

const char *ChipIntelliTimer::errorString(Error error) {
  switch (error) {
    case Error::None: return "none";
    case Error::InvalidTimer: return "invalid timer";
    case Error::InvalidPeriod: return "invalid period";
    case Error::InvalidCallback: return "invalid callback";
    case Error::ResourceBusy: return "timer busy";
    case Error::ClockUnavailable: return "APB clock unavailable";
    case Error::NotBegun: return "timer not begun";
    case Error::AlreadyBegun: return "timer already begun";
    case Error::Busy: return "timer operation already in progress";
    case Error::InterruptContext: return "timer control is not allowed in an ISR";
  }
  return "unknown";
}

void ChipIntelliTimer::handleInterrupt() {
  timer_clear_irq(kTimerBases[_timerNumber]);
  if (!_repeat) {
    _running = false;
  }

  Callback callback = _callback;
  CallbackWithArg callbackWithArg = _callbackWithArg;
  void *argument = _argument;
  if (callback != nullptr) {
    callback();
  } else if (callbackWithArg != nullptr) {
    callbackWithArg(argument);
  }
}

void ChipIntelliTimer::dispatch(uint8_t timerNumber) {
  ChipIntelliTimer *owner = s_owners[timerNumber];
  if (owner != nullptr) {
    owner->handleInterrupt();
  } else {
    timer_clear_irq(kTimerBases[timerNumber]);
  }
}

void ChipIntelliTimer::handleTimer0() { dispatch(0); }
void ChipIntelliTimer::handleTimer1() { dispatch(1); }
void ChipIntelliTimer::handleTimer2() { dispatch(2); }
void ChipIntelliTimer::handleTimer3() { dispatch(3); }
