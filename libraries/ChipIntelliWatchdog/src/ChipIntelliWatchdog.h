#pragma once

#include <Arduino.h>
#include <stdint.h>

class ChipIntelliWatchdogFactory;

class ChipIntelliWatchdogClass {
public:
  // This is one hardware countdown stage. With reset enabled, CI130X resets
  // after two consecutive stages expire without a feed (see README.md).
  static constexpr uint32_t DefaultTimeoutMs = 3000;

  enum class Error : uint8_t {
    None = 0,
    InvalidTimeout,
    ClockUnavailable,
    TimeoutTooLong,
    NotRunning,
    InterruptContext,
    InvalidLivenessMask,
  };

  ChipIntelliWatchdogClass(const ChipIntelliWatchdogClass &) = delete;
  ChipIntelliWatchdogClass &operator=(const ChipIntelliWatchdogClass &) =
      delete;

  // Enables the official IWDG with interrupt and whole-system reset enabled.
  // timeoutMs is the duration of one watchdog countdown stage.
  bool begin(uint32_t timeoutMs = DefaultTimeoutMs);
  bool beginSupervised(
      uint32_t timeoutMs = DefaultTimeoutMs,
      uint32_t requiredLiveness =
          CHIPINTELLI_LIVENESS_ARDUINO_LOOP |
          CHIPINTELLI_LIVENESS_SDK_AUDIO);

  // Clears a pending first-stage warning and reloads the counter.
  bool feed();
  bool reset() { return feed(); }
  bool heartbeat(uint32_t sources = CHIPINTELLI_LIVENESS_APPLICATION);
  uint32_t requiredLiveness() const;

  // Stops the watchdog through the official SDK driver. Calling end() more
  // than once is safe.
  void end();
  void disable() { end(); }

  bool isRunning() const;
  uint32_t timeoutMs() const;
  uint32_t reloadCount() const;
  uint32_t counterClockHz() const;
  uint32_t maximumTimeoutMs() const;

  Error lastError() const;
  const char *errorString() const;
  static const char *errorString(Error error);

private:
  friend class ChipIntelliWatchdogFactory;
  ChipIntelliWatchdogClass();

  void setError(Error error);

  bool _running;
  uint32_t _timeoutMs;
  uint32_t _reloadCount;
  Error _lastError;
};

extern ChipIntelliWatchdogClass &ChipIntelliWatchdog;
