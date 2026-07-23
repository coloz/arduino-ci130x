#include "Ticker.h"

#include <cmath>
#include <limits.h>

extern "C" {
#include "FreeRTOS.h"
#include "ci130x_core_misc.h"
#include "task.h"
#include "timers.h"
}

namespace {
constexpr uint8_t kMaximumTickers = 8;
constexpr uint32_t kCommandWaitMilliseconds = 100;

struct TickerSlot {
  TimerHandle_t timer;
  Ticker::Callback callback;
  Ticker::CallbackWithArg callbackWithArg;
  void *argument;
  bool allocated;
  bool active;
  bool repeat;
};

TickerSlot s_slots[kMaximumTickers] = {};

int8_t reserveSlot() {
  taskENTER_CRITICAL();
  for (uint8_t index = 0; index < kMaximumTickers; ++index) {
    if (!s_slots[index].allocated) {
      s_slots[index].allocated = true;
      taskEXIT_CRITICAL();
      return static_cast<int8_t>(index);
    }
  }
  taskEXIT_CRITICAL();
  return -1;
}

TickType_t commandWait() {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
    return 0;
  }
  if (xTaskGetCurrentTaskHandle() == xTimerGetTimerDaemonTaskHandle()) {
    return 0;
  }
  const TickType_t wait = pdMS_TO_TICKS(kCommandWaitMilliseconds);
  return wait > 0 ? wait : 1;
}

void tickerDispatch(TimerHandle_t timer) {
  TickerSlot *slot = static_cast<TickerSlot *>(pvTimerGetTimerID(timer));
  Ticker::Callback callback = nullptr;
  Ticker::CallbackWithArg callbackWithArg = nullptr;
  void *argument = nullptr;

  taskENTER_CRITICAL();
  if (slot != nullptr && slot->allocated && slot->timer == timer &&
      slot->active) {
    callback = slot->callback;
    callbackWithArg = slot->callbackWithArg;
    argument = slot->argument;
    if (!slot->repeat) {
      slot->active = false;
    }
  }
  taskEXIT_CRITICAL();

  if (callback != nullptr) {
    callback();
  } else if (callbackWithArg != nullptr) {
    callbackWithArg(argument);
  }
}
}  // namespace

Ticker::Ticker()
    : _timer(nullptr),
      _slot(-1),
      _operationInProgress(false),
      _lastError(Error::None) {}

Ticker::~Ticker() { detach(); }

bool Ticker::attach(float seconds, Callback callback) {
  return scheduleSeconds(seconds, true, callback, nullptr, nullptr);
}

bool Ticker::attach(float seconds, CallbackWithArg callback, void *argument) {
  return scheduleSeconds(seconds, true, nullptr, callback, argument);
}

bool Ticker::attach_ms(uint32_t milliseconds, Callback callback) {
  return scheduleMilliseconds(milliseconds, true, callback, nullptr, nullptr);
}

bool Ticker::attach_ms(uint32_t milliseconds, CallbackWithArg callback,
                       void *argument) {
  return scheduleMilliseconds(milliseconds, true, nullptr, callback,
                              argument);
}

bool Ticker::once(float seconds, Callback callback) {
  return scheduleSeconds(seconds, false, callback, nullptr, nullptr);
}

bool Ticker::once(float seconds, CallbackWithArg callback, void *argument) {
  return scheduleSeconds(seconds, false, nullptr, callback, argument);
}

bool Ticker::once_ms(uint32_t milliseconds, Callback callback) {
  return scheduleMilliseconds(milliseconds, false, callback, nullptr, nullptr);
}

bool Ticker::once_ms(uint32_t milliseconds, CallbackWithArg callback,
                     void *argument) {
  return scheduleMilliseconds(milliseconds, false, nullptr, callback,
                              argument);
}

bool Ticker::scheduleSeconds(float seconds, bool repeat, Callback callback,
                             CallbackWithArg callbackWithArg, void *argument) {
  if (!std::isfinite(seconds) || seconds <= 0.0f) {
    _lastError = Error::InvalidInterval;
    return false;
  }
  const double ticks =
      static_cast<double>(seconds) * static_cast<double>(configTICK_RATE_HZ);
  if (ticks > static_cast<double>(UINT32_MAX)) {
    _lastError = Error::InvalidInterval;
    return false;
  }
  uint32_t roundedUp = static_cast<uint32_t>(ticks);
  if (static_cast<double>(roundedUp) < ticks) {
    ++roundedUp;
  }
  return scheduleTicks(roundedUp, repeat, callback, callbackWithArg, argument);
}

bool Ticker::scheduleMilliseconds(uint32_t milliseconds, bool repeat,
                                  Callback callback,
                                  CallbackWithArg callbackWithArg,
                                  void *argument) {
  if (milliseconds == 0U) {
    _lastError = Error::InvalidInterval;
    return false;
  }
  const uint64_t ticks =
      (static_cast<uint64_t>(milliseconds) * configTICK_RATE_HZ + 999U) /
      1000U;
  if (ticks == 0U || ticks > UINT32_MAX) {
    _lastError = Error::InvalidInterval;
    return false;
  }
  return scheduleTicks(static_cast<uint32_t>(ticks), repeat, callback,
                       callbackWithArg, argument);
}

bool Ticker::scheduleTicks(uint32_t ticks, bool repeat, Callback callback,
                           CallbackWithArg callbackWithArg, void *argument) {
  if (!beginOperation()) {
    return false;
  }
  if (callback == nullptr && callbackWithArg == nullptr) {
    _lastError = Error::InvalidCallback;
    finishOperation();
    return false;
  }
  if (ticks == 0U) {
    _lastError = Error::InvalidInterval;
    finishOperation();
    return false;
  }

  if (!releaseTimer()) {
    finishOperation();
    return false;
  }
  const int8_t slotIndex = reserveSlot();
  if (slotIndex < 0) {
    _lastError = Error::NoSlot;
    finishOperation();
    return false;
  }
  TickerSlot &slot = s_slots[slotIndex];
  slot.callback = callback;
  slot.callbackWithArg = callbackWithArg;
  slot.argument = argument;
  slot.active = false;
  slot.repeat = repeat;

  TimerHandle_t timer = xTimerCreate("ArduinoTicker", ticks,
                                    repeat ? pdTRUE : pdFALSE, &slot,
                                    tickerDispatch);
  if (timer == nullptr) {
    taskENTER_CRITICAL();
    slot = {};
    taskEXIT_CRITICAL();
    _lastError = Error::AllocationFailed;
    finishOperation();
    return false;
  }

  taskENTER_CRITICAL();
  slot.timer = timer;
  slot.active = true;
  _timer = timer;
  _slot = slotIndex;
  taskEXIT_CRITICAL();

  if (xTimerStart(timer, commandWait()) != pdPASS) {
    // The timer was allocated but never started. Keep ownership until its
    // delete command has actually been accepted, otherwise a full timer queue
    // would leave an unreachable heap object behind.
    (void)releaseTimer();
    _lastError = Error::QueueFull;
    finishOperation();
    return false;
  }

  _lastError = Error::None;
  finishOperation();
  return true;
}

bool Ticker::beginOperation() {
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

void Ticker::finishOperation() {
  taskENTER_CRITICAL();
  _operationInProgress = false;
  taskEXIT_CRITICAL();
}

bool Ticker::releaseTimer() {
  TimerHandle_t timer = nullptr;
  int8_t slotIndex = -1;
  taskENTER_CRITICAL();
  timer = static_cast<TimerHandle_t>(_timer);
  slotIndex = _slot;
  if (timer == nullptr || slotIndex < 0 || slotIndex >= kMaximumTickers) {
    taskEXIT_CRITICAL();
    return true;
  }

  TickerSlot &slot = s_slots[slotIndex];
  if (!slot.allocated || slot.timer != timer) {
    // Do not attempt to delete a handle whose ownership cannot be proven.
    taskEXIT_CRITICAL();
    _lastError = Error::Busy;
    return false;
  }
  slot.active = false;
  taskEXIT_CRITICAL();

  const TickType_t wait = commandWait();
  const BaseType_t deleted = xTimerDelete(timer, wait);

  taskENTER_CRITICAL();
  if (deleted == pdPASS && _timer == timer && _slot == slotIndex) {
    TickerSlot &ownedSlot = s_slots[slotIndex];
    if (ownedSlot.allocated && ownedSlot.timer == timer) {
      ownedSlot = {};
    }
    _timer = nullptr;
    _slot = -1;
  }
  taskEXIT_CRITICAL();

  _lastError = deleted == pdPASS ? Error::None : Error::QueueFull;
  return deleted == pdPASS;
}

void Ticker::detach() {
  if (!beginOperation()) {
    return;
  }
  (void)releaseTimer();
  finishOperation();
}

bool Ticker::active() const {
  taskENTER_CRITICAL();
  const TimerHandle_t timer = static_cast<TimerHandle_t>(_timer);
  const int8_t slotIndex = _slot;
  if (timer == nullptr || slotIndex < 0 || slotIndex >= kMaximumTickers) {
    taskEXIT_CRITICAL();
    return false;
  }

  const TickerSlot &slot = s_slots[slotIndex];
  const bool result =
      slot.allocated && slot.timer == timer && slot.active;
  taskEXIT_CRITICAL();
  return result;
}

Ticker::Error Ticker::lastError() const { return _lastError; }

const char *Ticker::errorString(Error error) {
  switch (error) {
    case Error::None: return "none";
    case Error::InvalidInterval: return "invalid interval";
    case Error::InvalidCallback: return "invalid callback";
    case Error::NoSlot: return "no ticker slot";
    case Error::AllocationFailed: return "allocation failed";
    case Error::QueueFull: return "timer command queue full";
    case Error::Busy: return "ticker operation already in progress";
    case Error::InterruptContext: return "Ticker control is not allowed in an ISR";
  }
  return "unknown";
}
