#include "ChipIntelliCWSL.h"

#include <ArduinoEvent.h>

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

ChipIntelliCWSLClass ChipIntelliCWSLClass::_instance;
ChipIntelliCWSLClass &ChipIntelliCWSL = ChipIntelliCWSLClass::instance();

ChipIntelliCWSLClass &ChipIntelliCWSLClass::instance() { return _instance; }

// The only instance has static storage and is zero-initialized before dynamic
// initialization. Error::None is zero, so no startup code is needed here.
ChipIntelliCWSLClass::ChipIntelliCWSLClass() {}

bool ChipIntelliCWSLClass::begin(uint32_t timeoutMs) {
  if (!chipintelli_cwsl_profile_enabled()) {
    setLastError(Error::ProfileDisabled);
    return false;
  }

  taskENTER_CRITICAL();
  if (_begun) {
    _lastError = Error::None;
    taskEXIT_CRITICAL();
    return true;
  }
  _head = 0;
  _tail = 0;
  _callbackHead = 0;
  _callbackTail = 0;
  _callbackDispatchPending = false;
  _droppedRead = 0;
  _droppedCallbacks = 0;
  _accepting = true;
  _lastError = Error::None;
  taskEXIT_CRITICAL();

  chipintelli_cwsl_set_callback(receiveFromCore, this);
  if (!chipintelli_sdk_begin()) {
    end();
    setLastError(Error::SDKStartFailed);
    return false;
  }

  const uint32_t startedAt = millis();
  chipintelli_sdk_state_t sdkState = chipintelli_sdk_state();
  while (sdkState == CHIPINTELLI_SDK_STARTING) {
    if ((millis() - startedAt) >= timeoutMs) {
      end();
      setLastError(Error::Timeout);
      return false;
    }
    delay(1);
    sdkState = chipintelli_sdk_state();
  }
  if (sdkState != CHIPINTELLI_SDK_READY) {
    end();
    setLastError(Error::SDKFailed);
    return false;
  }

  taskENTER_CRITICAL();
  _begun = true;
  _lastError = Error::None;
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
  _droppedRead = 0;
  _droppedCallbacks = 0;
  _lastError = Error::None;
  taskEXIT_CRITICAL();
  chipintelli_cwsl_set_callback(nullptr, nullptr);
}

bool ChipIntelliCWSLClass::profileEnabled() const {
  return chipintelli_cwsl_profile_enabled();
}

bool ChipIntelliCWSLClass::isBegun() const { return begun(); }

bool ChipIntelliCWSLClass::begun() const {
  taskENTER_CRITICAL();
  const bool isBegun = _begun;
  taskEXIT_CRITICAL();
  return isBegun;
}

void ChipIntelliCWSLClass::setLastError(Error error) {
  _lastError = error;
}

bool ChipIntelliCWSLClass::learnCommand(uint32_t commandId,
                                        uint16_t groupId) {
  const bool accepted = commandId <= UINT16_MAX && groupId <= UINT8_MAX &&
                        begun() && chipintelli_cwsl_learn(
                            commandId, groupId,
                            CHIPINTELLI_CWSL_COMMAND_WORD);
  setLastError(accepted ? Error::None : Error::RequestRejected);
  return accepted;
}

bool ChipIntelliCWSLClass::learnWakeWord(uint32_t commandId,
                                         uint16_t groupId) {
  const bool accepted = commandId <= UINT16_MAX && groupId <= UINT8_MAX &&
                        begun() && chipintelli_cwsl_learn(
                            commandId, groupId,
                            CHIPINTELLI_CWSL_WAKE_WORD);
  setLastError(accepted ? Error::None : Error::RequestRejected);
  return accepted;
}

bool ChipIntelliCWSLClass::cancelLearning() {
  const bool accepted = begun() && chipintelli_cwsl_cancel();
  setLastError(accepted ? Error::None : Error::RequestRejected);
  return accepted;
}

bool ChipIntelliCWSLClass::eraseCommand(uint32_t commandId,
                                        uint16_t groupId) {
  const bool accepted = commandId <= UINT16_MAX && groupId <= UINT8_MAX &&
                        begun() && chipintelli_cwsl_erase(
                            commandId, groupId,
                            CHIPINTELLI_CWSL_COMMAND_WORD);
  setLastError(accepted ? Error::None : Error::RequestRejected);
  return accepted;
}

bool ChipIntelliCWSLClass::eraseWakeWord(uint32_t commandId,
                                         uint16_t groupId) {
  const bool accepted = commandId <= UINT16_MAX && groupId <= UINT8_MAX &&
                        begun() && chipintelli_cwsl_erase(
                            commandId, groupId,
                            CHIPINTELLI_CWSL_WAKE_WORD);
  setLastError(accepted ? Error::None : Error::RequestRejected);
  return accepted;
}

bool ChipIntelliCWSLClass::eraseCommands() {
  const bool accepted = begun() && chipintelli_cwsl_erase(
      UINT32_MAX, UINT16_MAX, CHIPINTELLI_CWSL_COMMAND_WORD);
  setLastError(accepted ? Error::None : Error::RequestRejected);
  return accepted;
}

bool ChipIntelliCWSLClass::eraseWakeWords() {
  const bool accepted = begun() && chipintelli_cwsl_erase(
      UINT32_MAX, UINT16_MAX, CHIPINTELLI_CWSL_WAKE_WORD);
  setLastError(accepted ? Error::None : Error::RequestRejected);
  return accepted;
}

bool ChipIntelliCWSLClass::eraseAll() {
  const bool accepted = begun() && chipintelli_cwsl_erase(
      UINT32_MAX, UINT16_MAX, CHIPINTELLI_CWSL_ALL_WORDS);
  setLastError(accepted ? Error::None : Error::RequestRejected);
  return accepted;
}

ChipIntelliCWSLState ChipIntelliCWSLClass::state() const {
  if (!begun()) return CWSLUnavailable;
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

ChipIntelliCWSLClass::Error ChipIntelliCWSLClass::lastError() const {
  // Error is one byte on this 32-bit target. It is diagnostic state shared by
  // all callers, so a volatile atomic-width load is sufficient and smaller.
  return _lastError;
}

const char *ChipIntelliCWSLClass::errorString() const {
  return errorString(lastError());
}

const char *ChipIntelliCWSLClass::errorString(Error error) {
  switch (error) {
    case Error::None: return "no error";
    case Error::ProfileDisabled: return "CWSL is disabled by the selected firmware profile";
    case Error::SDKStartFailed: return "ChipIntelli SDK startup failed";
    case Error::SDKFailed: return "ChipIntelli SDK initialization failed";
    case Error::Timeout: return "timed out waiting for ChipIntelli SDK initialization";
    case Error::RequestRejected:
      return "CWSL rejected the request; call begin first and check ID ranges, reserved IDs 199..208, command type, template, capacity, and state";
  }
  return "unknown CWSL error";
}

const char *ChipIntelliCWSLClass::stateName(ChipIntelliCWSLState state) {
  switch (state) {
    case CWSLIdle: return "idle";
    case CWSLRecognizing: return "recognizing";
    case CWSLLearning: return "learning";
    case CWSLDeleting: return "deleting";
    case CWSLUnavailable: return "unavailable";
  }
  return "unknown";
}

const char *ChipIntelliCWSLClass::eventName(ChipIntelliCWSLEventType type) {
  switch (type) {
    case CWSLLearningStarted: return "learning-started";
    case CWSLRecordingStarted: return "recording-started";
    case CWSLAttemptResult: return "attempt-result";
    case CWSLLearningSucceeded: return "learning-succeeded";
    case CWSLLearningFailed: return "learning-failed";
    case CWSLLearningCancelled: return "learning-cancelled";
    case CWSLDeleteSucceeded: return "delete-succeeded";
    case CWSLRecognized: return "recognized";
    case CWSLDeleteFailed: return "delete-failed";
  }
  return "unknown";
}

const char *ChipIntelliCWSLClass::resultName(
    ChipIntelliCWSLLearnResult result) {
  switch (result) {
    case CWSLRecordSucceeded: return "record-succeeded";
    case CWSLRecordFailed: return "record-failed";
    case CWSLRegistrationFinished: return "registration-finished";
    case CWSLRegistrationAborted: return "registration-aborted";
    case CWSLNotEnoughFrames: return "not-enough-frames";
    case CWSLInvalidData: return "invalid-data";
    case CWSLDefaultCommandConflict: return "default-command-conflict";
  }
  return "unknown";
}

const char *ChipIntelliCWSLClass::wordTypeName(ChipIntelliCWSLWordType type) {
  switch (type) {
    case CWSLCommandWord: return "command";
    case CWSLWakeWord: return "wake-word";
    case CWSLAllWords: return "all";
  }
  return "unknown";
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

__attribute__((noinline)) void ChipIntelliCWSLClass::enqueueOne(
    Event *queue, volatile uint8_t &head, volatile uint8_t &tail,
    volatile uint32_t &dropped, const Event &event) {
  const uint8_t writeAt = head;
  const uint8_t next = static_cast<uint8_t>((writeAt + 1U) % kStorageSize);
  if (next == tail) {
    ++dropped;
    return;
  }
  queue[writeAt] = event;
  head = next;
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
  enqueueOne(_queue, _head, _tail, _droppedRead, delivered);
  if (_contextCallback != nullptr || _callback != nullptr) {
    enqueueOne(_callbackQueue, _callbackHead, _callbackTail,
               _droppedCallbacks, delivered);
    if (!_callbackDispatchPending) {
      _callbackDispatchPending = true;
      scheduleCallback = true;
    }
  }
  taskEXIT_CRITICAL();

  if (scheduleCallback &&
      !chipintelli_arduino_post_event(dispatchEvent, this, 0U)) {
    taskENTER_CRITICAL();
    // No dispatcher token exists for these records. Drop the callback-side
    // backlog atomically so a later event cannot deliver stale notifications.
    ++_droppedCallbacks;
    _callbackTail = _callbackHead;
    _callbackDispatchPending = false;
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
        static_cast<uint8_t>((_callbackTail + 1U) % kStorageSize);
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
  _tail = static_cast<uint8_t>((tail + 1U) % kStorageSize);
  taskEXIT_CRITICAL();
  return true;
}

size_t ChipIntelliCWSLClass::pendingEvents() const {
  taskENTER_CRITICAL();
  const size_t pending =
      static_cast<uint8_t>((_head + kStorageSize - _tail) % kStorageSize);
  taskEXIT_CRITICAL();
  return pending;
}

void ChipIntelliCWSLClass::clearEvents() {
  taskENTER_CRITICAL();
  _tail = _head;
  taskEXIT_CRITICAL();
}

uint32_t ChipIntelliCWSLClass::droppedEvents() const {
  taskENTER_CRITICAL();
  const uint32_t dropped = _droppedRead + _droppedCallbacks;
  taskEXIT_CRITICAL();
  return dropped;
}

uint32_t ChipIntelliCWSLClass::droppedReadEvents() const {
  taskENTER_CRITICAL();
  const uint32_t dropped = _droppedRead;
  taskEXIT_CRITICAL();
  return dropped;
}

uint32_t ChipIntelliCWSLClass::droppedCallbackEvents() const {
  taskENTER_CRITICAL();
  const uint32_t dropped = _droppedCallbacks;
  taskEXIT_CRITICAL();
  return dropped;
}
