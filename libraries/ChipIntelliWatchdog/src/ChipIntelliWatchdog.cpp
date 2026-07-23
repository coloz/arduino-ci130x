#include "ChipIntelliWatchdog.h"

#include <limits.h>

extern "C" {
#include "FreeRTOS.h"
#include "ci130x_core_misc.h"
#include "ci130x_dpmu.h"
#include "ci130x_iwdg.h"
#include "ci130x_scu.h"
#include "platform_config.h"
#include "task.h"
}

namespace {
constexpr uint32_t kIwdgClockDivider = 16;
constexpr uint32_t kMillisecondsPerSecond = 1000;

uint32_t watchdogClockHz() {
  return get_src_clk() / kIwdgClockDivider;
}

bool timeoutToCount(uint32_t timeoutMs, uint32_t clockHz,
                    uint32_t &count) {
  if (timeoutMs == 0 || clockHz == 0) {
    return false;
  }

  // Round up so the configured stage is never shorter than requested.
  const uint64_t scaled = static_cast<uint64_t>(clockHz) * timeoutMs;
  const uint64_t result =
      (scaled + kMillisecondsPerSecond - 1U) / kMillisecondsPerSecond;
  if (result == 0 || result > UINT32_MAX) {
    return false;
  }
  count = static_cast<uint32_t>(result);
  return true;
}
}  // namespace

class ChipIntelliWatchdogFactory {
public:
  static ChipIntelliWatchdogClass &instance() {
    static ChipIntelliWatchdogClass instance;
    return instance;
  }
};

ChipIntelliWatchdogClass &ChipIntelliWatchdog =
    ChipIntelliWatchdogFactory::instance();

ChipIntelliWatchdogClass::ChipIntelliWatchdogClass()
    : _running(false),
      _timeoutMs(0),
      _reloadCount(0),
      _lastError(Error::None) {}

bool ChipIntelliWatchdogClass::begin(uint32_t timeoutMs) {
  if (check_curr_trap() != 0) {
    setError(Error::InterruptContext);
    return false;
  }
  if (timeoutMs == 0) {
    setError(Error::InvalidTimeout);
    return false;
  }

  const uint32_t clockHz = watchdogClockHz();
  if (clockHz == 0) {
    setError(Error::ClockUnavailable);
    return false;
  }

  uint32_t count = 0;
  if (!timeoutToCount(timeoutMs, clockHz, count)) {
    setError(Error::TimeoutTooLong);
    return false;
  }

  iwdg_init_t init = {};
  init.count = count;
  init.irq = iwdg_irqen_enable;
  init.res = iwdg_resen_enable;

  // The vendor driver performs an unlock/write/lock sequence. Keep that
  // sequence atomic with respect to other FreeRTOS tasks that may feed IWDG.
  taskENTER_CRITICAL();
  if (_running) {
    // A repeated begin() may arrive just before the second-stage reset.
    // Reload first so the complete reconfiguration has a fresh time window.
    iwdg_feed(IWDG);
  }
  scu_set_device_gate(IWDG, ENABLE);
  dpmu_iwdg_reset_system_config();
  iwdg_init(IWDG, init);
  iwdg_open(IWDG);
  _timeoutMs = timeoutMs;
  _reloadCount = count;
  _running = true;
  _lastError = Error::None;
  taskEXIT_CRITICAL();
  return true;
}

bool ChipIntelliWatchdogClass::feed() {
  if (check_curr_trap() != 0) {
    setError(Error::InterruptContext);
    return false;
  }

  taskENTER_CRITICAL();
  if (!_running) {
    _lastError = Error::NotRunning;
    taskEXIT_CRITICAL();
    return false;
  }

  iwdg_feed(IWDG);
  _lastError = Error::None;
  taskEXIT_CRITICAL();
  return true;
}

void ChipIntelliWatchdogClass::end() {
  if (check_curr_trap() != 0) {
    setError(Error::InterruptContext);
    return;
  }

  taskENTER_CRITICAL();
  if (_running) {
    // Clear a possible first-stage warning before disabling reset/interrupt,
    // then restore the peripheral routing and clock to their idle state.
    iwdg_feed(IWDG);
    iwdg_close(IWDG);
    dpmu_iwdg_reset_none_config();
    scu_set_device_gate(IWDG, DISABLE);
  }
  _running = false;
  _timeoutMs = 0;
  _reloadCount = 0;
  _lastError = Error::None;
  taskEXIT_CRITICAL();
}

bool ChipIntelliWatchdogClass::isRunning() const { return _running; }

uint32_t ChipIntelliWatchdogClass::timeoutMs() const { return _timeoutMs; }

uint32_t ChipIntelliWatchdogClass::reloadCount() const {
  return _reloadCount;
}

uint32_t ChipIntelliWatchdogClass::counterClockHz() const {
  return watchdogClockHz();
}

uint32_t ChipIntelliWatchdogClass::maximumTimeoutMs() const {
  const uint32_t clockHz = watchdogClockHz();
  if (clockHz == 0) {
    return 0;
  }
  const uint64_t maximum =
      (static_cast<uint64_t>(UINT32_MAX) * kMillisecondsPerSecond) /
      clockHz;
  return maximum > UINT32_MAX ? UINT32_MAX
                              : static_cast<uint32_t>(maximum);
}

ChipIntelliWatchdogClass::Error ChipIntelliWatchdogClass::lastError() const {
  return _lastError;
}

const char *ChipIntelliWatchdogClass::errorString() const {
  return errorString(_lastError);
}

const char *ChipIntelliWatchdogClass::errorString(Error error) {
  switch (error) {
    case Error::None: return "none";
    case Error::InvalidTimeout: return "timeout must be greater than zero";
    case Error::ClockUnavailable: return "IWDG source clock is unavailable";
    case Error::TimeoutTooLong: return "timeout exceeds the IWDG counter";
    case Error::NotRunning: return "watchdog is not running";
    case Error::InterruptContext: return "watchdog control is not allowed in an ISR";
  }
  return "unknown error";
}

void ChipIntelliWatchdogClass::setError(Error error) { _lastError = error; }
