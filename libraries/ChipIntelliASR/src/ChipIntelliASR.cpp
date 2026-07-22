#include "ChipIntelliASR.h"

#include <string.h>

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

ChipIntelliASRClass ChipIntelliASR;

namespace {
// CI1306 is built as rv32imafc (without the RISC-V A extension). This is a
// single-producer/single-consumer queue on one CPU, so naturally aligned byte
// and word accesses plus compiler barriers avoid a libatomic dependency.
inline void compilerBarrier() {
  __asm__ __volatile__("" ::: "memory");
}
}  // namespace

ChipIntelliASRClass::ChipIntelliASRClass()
    : _head(0),
      _tail(0),
      _dropped(0),
      _callback(nullptr),
      _contextCallback(nullptr),
      _callbackContext(nullptr),
      _begun(false) {}

bool ChipIntelliASRClass::begin() {
  if (!_begun) {
    // The vendor ASR/audio application is intentionally not started during
    // Arduino boot. This explicit begin() call transfers the required pins and
    // peripherals to the SDK initialization task.
    if (!chipintelli_sdk_begin()) {
      return false;
    }
    _head = 0;
    _tail = 0;
    _dropped = 0;
    chipintelli_asr_set_callback(receiveFromCore, this);
    _begun = true;
  }
  return true;
}

void ChipIntelliASRClass::end() {
  if (_begun) {
    chipintelli_asr_set_callback(nullptr, nullptr);
    _begun = false;
  }
}

void ChipIntelliASRClass::onResult(ResultCallback callback) {
  taskENTER_CRITICAL();
  _callback = callback;
  _contextCallback = nullptr;
  _callbackContext = nullptr;
  taskEXIT_CRITICAL();
}

void ChipIntelliASRClass::onResult(ContextCallback callback, void *context) {
  taskENTER_CRITICAL();
  _contextCallback = callback;
  _callbackContext = context;
  _callback = nullptr;
  taskEXIT_CRITICAL();
}

void ChipIntelliASRClass::receiveFromCore(
    const chipintelli_asr_result_t *result, void *context) {
  if (result == nullptr || context == nullptr) {
    return;
  }
  static_cast<ChipIntelliASRClass *>(context)->enqueue(*result);
}

void ChipIntelliASRClass::enqueue(const chipintelli_asr_result_t &source) {
  uint8_t head = _head;
  uint8_t next = static_cast<uint8_t>((head + 1U) % kQueueSize);
  compilerBarrier();
  if (next == _tail) {
    ++_dropped;
    return;
  }

  Result &result = _queue[head];
  result.commandId = source.command_id;
  result.semanticId = source.semantic_id;
  result.score = source.score;
  result.frames = source.frames;
  if (source.text != nullptr) {
    strncpy(result.text, source.text, Result::kTextCapacity - 1U);
    result.text[Result::kTextCapacity - 1U] = '\0';
  } else {
    result.text[0] = '\0';
  }

  compilerBarrier();
  _head = next;

  taskENTER_CRITICAL();
  ContextCallback contextCallback = _contextCallback;
  ResultCallback callback = _callback;
  void *callbackContext = _callbackContext;
  taskEXIT_CRITICAL();
  if (contextCallback != nullptr) {
    contextCallback(result, callbackContext);
  } else if (callback != nullptr) {
    callback(result);
  }
}

bool ChipIntelliASRClass::available() const {
  uint8_t tail = _tail;
  compilerBarrier();
  return tail != _head;
}

bool ChipIntelliASRClass::read(Result &result) {
  uint8_t tail = _tail;
  compilerBarrier();
  if (tail == _head) {
    return false;
  }
  result = _queue[tail];
  compilerBarrier();
  _tail = static_cast<uint8_t>((tail + 1U) % kQueueSize);
  return true;
}

uint32_t ChipIntelliASRClass::droppedResults() const {
  compilerBarrier();
  return _dropped;
}
