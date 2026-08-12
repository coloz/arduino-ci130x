#include "ChipIntelliCWSL.h"

#include <ArduinoEvent.h>

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

ChipIntelliCWSLClass ChipIntelliCWSL;

static bool validExactTemplate(uint32_t commandId, uint16_t groupId) {
  return commandId <= UINT16_MAX && groupId <= UINT8_MAX;
}

ChipIntelliCWSLClass::ChipIntelliCWSLClass()
    : _head(0),
      _tail(0),
      _callbackHead(0),
      _callbackTail(0),
      _dropped(0),
      _callback(nullptr),
      _contextCallback(nullptr),
      _callbackContext(nullptr),
      _begun(false),
      _accepting(false),
      _callbackDispatchPending(false) {}

bool ChipIntelliCWSLClass::begin(uint32_t timeoutMs) {
  if (!chipintelli_cwsl_profile_enabled()) {
    return false;
  }

  taskENTER_CRITICAL();
  if (_begun) {
    taskEXIT_CRITICAL();
    return true;
  }
  _head = 0;
  _tail = 0;
  _callbackHead = 0;
  _callbackTail = 0;
  _callbackDispatchPending = false;
  _dropped = 0;
  _accepting = true;
  taskEXIT_CRITICAL();

  chipintelli_cwsl_set_callback(receiveFromCore, this);
  if (!chipintelli_sdk_begin()) {
    end();
    return false;
  }

  const uint32_t startedAt = millis();
  chipintelli_sdk_state_t sdkState = chipintelli_sdk_state();
  while (sdkState == CHIPINTELLI_SDK_STARTING) {
    if ((millis() - startedAt) >= timeoutMs) {
      end();
      return false;
    }
    delay(1);
    sdkState = chipintelli_sdk_state();
  }
  if (sdkState != CHIPINTELLI_SDK_READY) {
    end();
    return false;
  }

  taskENTER_CRITICAL();
  _begun = true;
  taskEXIT_CRITICAL();
  return true;
}

void ChipIntelliCWSLClass::end() {
  taskENTER_CRITICAL();
  _accepting = false;
  _begun = false;
  _head = 0;
  _tail = 0;
  _callbackHead = 0;
  _callbackTail = 0;
  _callbackDispatchPending = false;
  _dropped = 0;
  taskEXIT_CRITICAL();
  chipintelli_cwsl_set_callback(nullptr, nullptr);
}

bool ChipIntelliCWSLClass::profileEnabled() const {
  return chipintelli_cwsl_profile_enabled();
}

bool ChipIntelliCWSLClass::begun() const {
  taskENTER_CRITICAL();
  const bool isBegun = _begun;
  taskEXIT_CRITICAL();
  return isBegun;
}

bool ChipIntelliCWSLClass::learnCommand(uint32_t commandId,
                                        uint16_t groupId) {
  return validExactTemplate(commandId, groupId) && begun() &&
         chipintelli_cwsl_learn(
                        commandId, groupId, CHIPINTELLI_CWSL_COMMAND_WORD);
}

bool ChipIntelliCWSLClass::learnWakeWord(uint32_t commandId,
                                         uint16_t groupId) {
  return validExactTemplate(commandId, groupId) && begun() &&
         chipintelli_cwsl_learn(
                        commandId, groupId, CHIPINTELLI_CWSL_WAKE_WORD);
}

bool ChipIntelliCWSLClass::cancelLearning() {
  return begun() && chipintelli_cwsl_cancel();
}

bool ChipIntelliCWSLClass::eraseCommand(uint32_t commandId,
                                        uint16_t groupId) {
  return validExactTemplate(commandId, groupId) && begun() &&
         chipintelli_cwsl_erase(
                        commandId, groupId, CHIPINTELLI_CWSL_COMMAND_WORD);
}

bool ChipIntelliCWSLClass::eraseWakeWord(uint32_t commandId,
                                         uint16_t groupId) {
  return validExactTemplate(commandId, groupId) && begun() &&
         chipintelli_cwsl_erase(
                        commandId, groupId, CHIPINTELLI_CWSL_WAKE_WORD);
}

bool ChipIntelliCWSLClass::eraseCommands() {
  return begun() && chipintelli_cwsl_erase(
                        UINT32_MAX, UINT16_MAX, CHIPINTELLI_CWSL_COMMAND_WORD);
}

bool ChipIntelliCWSLClass::eraseWakeWords() {
  return begun() && chipintelli_cwsl_erase(
                        UINT32_MAX, UINT16_MAX, CHIPINTELLI_CWSL_WAKE_WORD);
}

bool ChipIntelliCWSLClass::eraseAll() {
  return begun() && chipintelli_cwsl_erase(
                        UINT32_MAX, UINT16_MAX, CHIPINTELLI_CWSL_ALL_WORDS);
}

ChipIntelliCWSLState ChipIntelliCWSLClass::state() const {
  return static_cast<ChipIntelliCWSLState>(chipintelli_cwsl_state());
}

int ChipIntelliCWSLClass::commandCount() const {
  return begun() ? chipintelli_cwsl_template_count(
                       CHIPINTELLI_CWSL_COMMAND_WORD)
                 : -1;
}

int ChipIntelliCWSLClass::wakeWordCount() const {
  return begun() ? chipintelli_cwsl_template_count(CHIPINTELLI_CWSL_WAKE_WORD)
                 : -1;
}

int ChipIntelliCWSLClass::templateCount() const {
  return begun() ? chipintelli_cwsl_template_count(CHIPINTELLI_CWSL_ALL_WORDS)
                 : -1;
}

int ChipIntelliCWSLClass::remainingTemplates() const {
  return begun() ? chipintelli_cwsl_remaining_templates() : -1;
}

int ChipIntelliCWSLClass::maxTemplates() const {
  return begun() ? chipintelli_cwsl_max_templates() : -1;
}

void ChipIntelliCWSLClass::onEvent(EventCallback callback) {
  taskENTER_CRITICAL();
  // Callback records belong to the registration that was active when they
  // were received. Do not deliver an old backlog through a replacement.
  _callbackTail = _callbackHead;
  _callback = callback;
  _contextCallback = nullptr;
  _callbackContext = nullptr;
  taskEXIT_CRITICAL();
}

void ChipIntelliCWSLClass::onEvent(ContextCallback callback, void *context) {
  taskENTER_CRITICAL();
  _callbackTail = _callbackHead;
  _contextCallback = callback;
  _callbackContext = context;
  _callback = nullptr;
  taskEXIT_CRITICAL();
}

void ChipIntelliCWSLClass::receiveFromCore(
    const chipintelli_cwsl_event_t *event, void *context) {
  if (event != nullptr && context != nullptr) {
    static_cast<ChipIntelliCWSLClass *>(context)->enqueue(*event);
  }
}

void ChipIntelliCWSLClass::enqueue(const chipintelli_cwsl_event_t &source) {
  const Event delivered = {
      static_cast<ChipIntelliCWSLEventType>(source.type),
      static_cast<ChipIntelliCWSLWordType>(source.word_type),
      source.attempt,
      static_cast<ChipIntelliCWSLLearnResult>(source.result),
      source.command_id,
      source.group_id,
      source.distance
  };

  bool scheduleCallback = false;
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
  if (_contextCallback != nullptr || _callback != nullptr) {
    const uint8_t callbackHead = _callbackHead;
    const uint8_t callbackNext =
        static_cast<uint8_t>((callbackHead + 1U) % kQueueSize);
    if (callbackNext == _callbackTail) {
      ++_dropped;
    } else {
      _callbackQueue[callbackHead] = delivered;
      _callbackHead = callbackNext;
      if (!_callbackDispatchPending) {
        _callbackDispatchPending = true;
        scheduleCallback = true;
      }
    }
  }
  taskEXIT_CRITICAL();

  if (scheduleCallback &&
      !chipintelli_arduino_post_event(dispatchEvent, this, 0U)) {
    taskENTER_CRITICAL();
    // No dispatcher token exists for these records. Drop the callback-side
    // backlog atomically so a later event cannot deliver stale notifications.
    _callbackTail = _callbackHead;
    _callbackDispatchPending = false;
    ++_dropped;
    taskEXIT_CRITICAL();
  }
  chipintelli_arduino_wake();
}

void ChipIntelliCWSLClass::dispatchEvent(void *context, uint32_t value) {
  (void)value;
  if (context != nullptr) {
    static_cast<ChipIntelliCWSLClass *>(context)->dispatchCallbacks();
  }
}

void ChipIntelliCWSLClass::dispatchCallbacks() {
  for (;;) {
    taskENTER_CRITICAL();
    if (_callbackTail == _callbackHead) {
      _callbackDispatchPending = false;
      taskEXIT_CRITICAL();
      return;
    }
    const Event delivered = _callbackQueue[_callbackTail];
    _callbackTail =
        static_cast<uint8_t>((_callbackTail + 1U) % kQueueSize);
    ContextCallback contextCallback = _contextCallback;
    EventCallback callback = _callback;
    void *callbackContext = _callbackContext;
    taskEXIT_CRITICAL();

    if (contextCallback != nullptr) {
      contextCallback(delivered, callbackContext);
    } else if (callback != nullptr) {
      callback(delivered);
    }
  }
}

bool ChipIntelliCWSLClass::available() const {
  taskENTER_CRITICAL();
  const bool hasEvent = _tail != _head;
  taskEXIT_CRITICAL();
  return hasEvent;
}

bool ChipIntelliCWSLClass::read(Event &event) {
  taskENTER_CRITICAL();
  const uint8_t tail = _tail;
  if (tail == _head) {
    taskEXIT_CRITICAL();
    return false;
  }
  event = _queue[tail];
  _tail = static_cast<uint8_t>((tail + 1U) % kQueueSize);
  taskEXIT_CRITICAL();
  return true;
}

uint32_t ChipIntelliCWSLClass::droppedEvents() const {
  taskENTER_CRITICAL();
  const uint32_t dropped = _dropped;
  taskEXIT_CRITICAL();
  return dropped;
}
