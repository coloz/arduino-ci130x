#pragma once

#include <stdint.h>

class ChipIntelliTimer {
 public:
  using Callback = void (*)();
  using CallbackWithArg = void (*)(void *);

  static constexpr uint8_t Automatic = 0xFF;

  enum class Error : uint8_t {
    None = 0,
    InvalidTimer,
    InvalidPeriod,
    InvalidCallback,
    ResourceBusy,
    ClockUnavailable,
    NotBegun,
    AlreadyBegun,
    Busy,
    InterruptContext,
  };

  ChipIntelliTimer();
  ~ChipIntelliTimer();

  ChipIntelliTimer(const ChipIntelliTimer &) = delete;
  ChipIntelliTimer &operator=(const ChipIntelliTimer &) = delete;

  // Automatically claims the first free hardware timer and starts it.
  bool begin(uint32_t periodMicros, Callback callback, bool repeat = true);
  bool begin(uint32_t periodMicros, CallbackWithArg callback, void *argument,
             bool repeat = true);

  // Claims and starts a hardware timer by number. TIMER3 is reserved by the
  // official BLE stack in the current profile and returns ResourceBusy.
  bool begin(uint8_t timerNumber, uint32_t periodMicros, Callback callback,
             bool repeat = true);
  bool begin(uint8_t timerNumber, uint32_t periodMicros,
             CallbackWithArg callback, void *argument, bool repeat = true);

  void end();
  bool start();
  void stop();
  bool restart();
  bool setPeriod(uint32_t periodMicros);

  bool begun() const;
  bool running() const;
  bool repeating() const;
  int8_t timerNumber() const;
  uint32_t period() const;
  Error lastError() const;

  static const char *errorString(Error error);

 private:
  bool beginInternal(uint8_t timerNumber, uint32_t periodMicros,
                     Callback callback, CallbackWithArg callbackWithArg,
                     void *argument, bool repeat);
  bool acquire(uint8_t timerNumber);
  bool configure(uint32_t periodMicros);
  bool beginOperation();
  void finishOperation();
  void endInternal();
  void handleInterrupt();

  static void dispatch(uint8_t timerNumber);
  static void handleTimer0();
  static void handleTimer1();
  static void handleTimer2();
  static void handleTimer3();
  static ChipIntelliTimer *s_owners[4];

  Callback _callback;
  CallbackWithArg _callbackWithArg;
  void *_argument;
  uint32_t _periodMicros;
  uint8_t _timerNumber;
  bool _repeat;
  bool _begun;
  bool _operationInProgress;
  volatile bool _running;
  Error _lastError;
};
