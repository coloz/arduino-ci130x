#include "ChipIntelliASR.h"

#include <string.h>

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

ChipIntelliASRClass ChipIntelliASR;

ChipIntelliASRClass::ChipIntelliASRClass()
    : _head(0),
      _tail(0),
      _dropped(0),
      _callback(nullptr),
      _contextCallback(nullptr),
      _callbackContext(nullptr),
      _begun(false),
      _accepting(false) {}

bool ChipIntelliASRClass::begin(uint32_t timeoutMs) {
#if defined(NO_ASR_FLOW) && NO_ASR_FLOW
  (void)timeoutMs;
  return false;
#else
  taskENTER_CRITICAL();
  if (_begun) {
    taskEXIT_CRITICAL();
    return true;
  }
  _head = 0;
  _tail = 0;
  _dropped = 0;
  _accepting = true;
  taskEXIT_CRITICAL();

  // Register before starting the task so a fast or already-running shared SDK
  // cannot publish a result into an unobserved window.
  chipintelli_asr_set_callback(receiveFromCore, this);
  if (!chipintelli_sdk_begin()) {
    end();
    return false;
  }

  const uint32_t startedAt = millis();
  chipintelli_sdk_state_t state = chipintelli_sdk_state();
  while (state == CHIPINTELLI_SDK_STARTING) {
    if ((millis() - startedAt) >= timeoutMs) {
      end();
      return false;
    }
    delay(1);
    state = chipintelli_sdk_state();
  }

  if (state != CHIPINTELLI_SDK_READY) {
    end();
    return false;
  }

  taskENTER_CRITICAL();
  _begun = true;
  taskEXIT_CRITICAL();
  return true;
#endif
}

void ChipIntelliASRClass::end() {
  taskENTER_CRITICAL();
  _accepting = false;
  _begun = false;
  _head = 0;
  _tail = 0;
  _dropped = 0;
  taskEXIT_CRITICAL();
  chipintelli_asr_set_callback(nullptr, nullptr);
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
  Result delivered;
  delivered.commandId = source.command_id;
  delivered.semanticId = source.semantic_id;
  delivered.score = source.score;
  delivered.frames = source.frames;
  if (source.text != nullptr) {
    const size_t length = strlen(source.text);
    const size_t copyLength =
        length < Result::kTextCapacity ? length : Result::kTextCapacity - 1U;
    memcpy(delivered.text, source.text, copyLength);
    delivered.text[copyLength] = '\0';
    delivered.textTruncated = length >= Result::kTextCapacity;
  } else {
    delivered.text[0] = '\0';
    delivered.textTruncated = false;
  }

  taskENTER_CRITICAL();
  if (!_accepting) {
    taskEXIT_CRITICAL();
    return;
  }

  const uint8_t head = _head;
  const uint8_t next = static_cast<uint8_t>((head + 1U) % kQueueSize);
  if (next == _tail) {
    ++_dropped;
  } else {
    _queue[head] = delivered;
    _head = next;
  }

  ContextCallback contextCallback = _contextCallback;
  ResultCallback callback = _callback;
  void *callbackContext = _callbackContext;
  taskEXIT_CRITICAL();
  if (contextCallback != nullptr) {
    contextCallback(delivered, callbackContext);
  } else if (callback != nullptr) {
    callback(delivered);
  }
}

bool ChipIntelliASRClass::available() const {
  taskENTER_CRITICAL();
  const bool hasResult = _tail != _head;
  taskEXIT_CRITICAL();
  return hasResult;
}

bool ChipIntelliASRClass::read(Result &result) {
  taskENTER_CRITICAL();
  const uint8_t tail = _tail;
  if (tail == _head) {
    taskEXIT_CRITICAL();
    return false;
  }
  result = _queue[tail];
  _tail = static_cast<uint8_t>((tail + 1U) % kQueueSize);
  taskEXIT_CRITICAL();
  return true;
}

uint32_t ChipIntelliASRClass::droppedResults() const {
  taskENTER_CRITICAL();
  const uint32_t dropped = _dropped;
  taskEXIT_CRITICAL();
  return dropped;
}
