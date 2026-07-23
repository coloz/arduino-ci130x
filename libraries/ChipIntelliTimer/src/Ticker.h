#pragma once

#include <stdint.h>

class Ticker {
 public:
  using Callback = void (*)();
  using CallbackWithArg = void (*)(void *);

  enum class Error : uint8_t {
    None = 0,
    InvalidInterval,
    InvalidCallback,
    NoSlot,
    AllocationFailed,
    QueueFull,
    Busy,
    InterruptContext,
  };

  Ticker();
  ~Ticker();

  Ticker(const Ticker &) = delete;
  Ticker &operator=(const Ticker &) = delete;

  bool attach(float seconds, Callback callback);
  bool attach(float seconds, CallbackWithArg callback, void *argument);
  bool attach_ms(uint32_t milliseconds, Callback callback);
  bool attach_ms(uint32_t milliseconds, CallbackWithArg callback,
                 void *argument);

  bool once(float seconds, Callback callback);
  bool once(float seconds, CallbackWithArg callback, void *argument);
  bool once_ms(uint32_t milliseconds, Callback callback);
  bool once_ms(uint32_t milliseconds, CallbackWithArg callback,
               void *argument);

  void detach();
  bool active() const;
  Error lastError() const;

  static const char *errorString(Error error);

 private:
  bool scheduleSeconds(float seconds, bool repeat, Callback callback,
                       CallbackWithArg callbackWithArg, void *argument);
  bool scheduleMilliseconds(uint32_t milliseconds, bool repeat,
                            Callback callback,
                            CallbackWithArg callbackWithArg, void *argument);
  bool scheduleTicks(uint32_t ticks, bool repeat, Callback callback,
                     CallbackWithArg callbackWithArg, void *argument);
  bool beginOperation();
  void finishOperation();
  bool releaseTimer();

  void *_timer;
  int8_t _slot;
  bool _operationInProgress;
  Error _lastError;
};

using ChipIntelliTicker = Ticker;
