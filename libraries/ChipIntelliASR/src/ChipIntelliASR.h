#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Arduino.h owns the stable C ABI between the core's SDK hook and this
// library: chipintelli_asr_result_t, chipintelli_asr_callback_t, and
// chipintelli_asr_set_callback(). The core owns the pointed-to text; this
// library copies it before returning from the hook.

struct ChipIntelliASRResult {
  static constexpr size_t kTextCapacity = 64;

  uint16_t commandId;
  uint32_t semanticId;
  int16_t score;
  uint16_t frames;
  char text[kTextCapacity];
};

class ChipIntelliASRClass {
public:
  using Result = ChipIntelliASRResult;
  using ResultCallback = void (*)(const Result &result);
  using ContextCallback = void (*)(const Result &result, void *context);

  ChipIntelliASRClass();

  bool begin();
  void end();

  // Callback executes in the CI13XX SDK message task. Keep it short and do
  // lengthy work from loop() using available()/read().
  void onResult(ResultCallback callback);
  void onResult(ContextCallback callback, void *context);

  bool available() const;
  bool read(Result &result);
  uint32_t droppedResults() const;

private:
  static constexpr uint8_t kQueueSize = 4;

  static void receiveFromCore(const chipintelli_asr_result_t *result,
                              void *context);
  void enqueue(const chipintelli_asr_result_t &result);

  Result _queue[kQueueSize];
  volatile uint8_t _head;
  volatile uint8_t _tail;
  volatile uint32_t _dropped;
  ResultCallback _callback;
  ContextCallback _contextCallback;
  void *_callbackContext;
  bool _begun;
};

extern ChipIntelliASRClass ChipIntelliASR;
