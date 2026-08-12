#pragma once

#include <Arduino.h>
#include <stdint.h>

enum ChipIntelliCWSLWordType : uint8_t {
  CWSLCommandWord = CHIPINTELLI_CWSL_COMMAND_WORD,
  CWSLWakeWord = CHIPINTELLI_CWSL_WAKE_WORD,
  CWSLAllWords = CHIPINTELLI_CWSL_ALL_WORDS
};

enum ChipIntelliCWSLState : uint8_t {
  CWSLIdle = CHIPINTELLI_CWSL_IDLE,
  CWSLRecognizing = CHIPINTELLI_CWSL_RECOGNIZING,
  CWSLLearning = CHIPINTELLI_CWSL_LEARNING,
  CWSLDeleting = CHIPINTELLI_CWSL_DELETING,
  CWSLUnavailable = CHIPINTELLI_CWSL_UNAVAILABLE
};

enum ChipIntelliCWSLEventType : uint8_t {
  CWSLLearningStarted = CHIPINTELLI_CWSL_LEARNING_STARTED,
  // The vendor record-start request was queued (not a hardware capture IRQ).
  CWSLRecordingStarted = CHIPINTELLI_CWSL_RECORDING_STARTED,
  CWSLAttemptResult = CHIPINTELLI_CWSL_ATTEMPT_RESULT,
  CWSLLearningSucceeded = CHIPINTELLI_CWSL_LEARNING_SUCCEEDED,
  CWSLLearningFailed = CHIPINTELLI_CWSL_LEARNING_FAILED,
  CWSLLearningCancelled = CHIPINTELLI_CWSL_LEARNING_CANCELLED,
  CWSLDeleteSucceeded = CHIPINTELLI_CWSL_DELETE_SUCCEEDED,
  CWSLRecognized = CHIPINTELLI_CWSL_RECOGNIZED,
  CWSLDeleteFailed = CHIPINTELLI_CWSL_DELETE_FAILED
};

// Values mirror the vendor cwsl_reg_result_t returned with attempt events.
enum ChipIntelliCWSLLearnResult : uint8_t {
  CWSLRecordSucceeded = 0,
  CWSLRecordFailed,
  CWSLRegistrationFinished,
  CWSLRegistrationAborted,
  CWSLNotEnoughFrames,
  CWSLInvalidData,
  CWSLDefaultCommandConflict
};

struct ChipIntelliCWSLEvent {
  ChipIntelliCWSLEventType type;
  ChipIntelliCWSLWordType wordType;
  uint8_t attempt;
  ChipIntelliCWSLLearnResult result;
  uint32_t commandId;
  uint16_t groupId;
  uint32_t distance;
};

class ChipIntelliCWSLClass {
public:
  using Event = ChipIntelliCWSLEvent;
  using EventCallback = void (*)(const Event &event);
  using ContextCallback = void (*)(const Event &event, void *context);

  ChipIntelliCWSLClass();

  // Returns false when the Standard offline ASR profile is selected.
  bool begin(uint32_t timeoutMs = 10000U);
  void end();
  bool profileEnabled() const;

  // Call begin/learn/cancel/erase/query APIs only from task context (for
  // example setup(), loop(), or an RTOS task), never from an ISR or hardware
  // timer callback. commandId must identify a command in the active cmd_info
  // resource; IDs 199 through 208 are reserved by the packaged voice flow.
  // After an untagged vendor record-end is quarantined, new learning calls
  // return false until MCU restart; recognition and erase APIs remain usable.
  bool learnCommand(uint32_t commandId, uint16_t groupId = 0);
  bool learnWakeWord(uint32_t commandId, uint16_t groupId = 0);
  // The vendor has no stop acknowledgement. Cancellation is accepted only
  // before CWSLLearningStarted; a started recording continues to completion.
  bool cancelLearning();

  bool eraseCommand(uint32_t commandId, uint16_t groupId = 0);
  bool eraseWakeWord(uint32_t commandId, uint16_t groupId = 0);
  bool eraseCommands();
  bool eraseWakeWords();
  bool eraseAll();

  ChipIntelliCWSLState state() const;
  int commandCount() const;
  int wakeWordCount() const;
  int templateCount() const;
  int remainingTemplates() const;
  int maxTemplates() const;

  void onEvent(EventCallback callback);
  void onEvent(ContextCallback callback, void *context);
  bool available() const;
  bool read(Event &event);
  uint32_t droppedEvents() const;

private:
  static constexpr uint8_t kQueueSize = 8;

  static void receiveFromCore(const chipintelli_cwsl_event_t *event,
                              void *context);
  static void dispatchEvent(void *context, uint32_t value);
  void dispatchCallbacks();
  void enqueue(const chipintelli_cwsl_event_t &event);
  bool begun() const;

  Event _queue[kQueueSize];
  Event _callbackQueue[kQueueSize];
  volatile uint8_t _head;
  volatile uint8_t _tail;
  volatile uint8_t _callbackHead;
  volatile uint8_t _callbackTail;
  volatile uint32_t _dropped;
  EventCallback _callback;
  ContextCallback _contextCallback;
  void *_callbackContext;
  volatile bool _begun;
  volatile bool _accepting;
  volatile bool _callbackDispatchPending;
};

extern ChipIntelliCWSLClass ChipIntelliCWSL;
