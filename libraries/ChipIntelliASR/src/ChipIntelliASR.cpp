#include "ChipIntelliASR.h"

#include <string.h>

extern "C" {
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
}

ChipIntelliASRClass ChipIntelliASRClass::_instance;
ChipIntelliASRClass &ChipIntelliASR = ChipIntelliASRClass::instance();

ChipIntelliASRClass &ChipIntelliASRClass::instance() { return _instance; }

/**
 * @brief 初始化一个尚未启动的 ASR 接收器，并清空回调与队列状态。
 */
ChipIntelliASRClass::ChipIntelliASRClass()
    : _resultQueue(nullptr),
      _lifecycleQueue(nullptr),
      _nextSequence(0),
      _dropped(0),
      _droppedEvents(0),
      _callback(nullptr),
      _contextCallback(nullptr),
      _callbackContext(nullptr),
      _startupCallback(nullptr),
      _wakeupCallback(nullptr),
      _timeoutCallback(nullptr),
      _handlerCount(0),
      _lastError(Error::None),
      _begun(false),
      _dispatching(false),
      _accepting(false) {
}

/**
 * @brief 启动或接入共享 CI13XX SDK，等待 ASR 音频输入通路就绪。
 *
 * 监听器会在启动 SDK 前注册，避免共享 SDK 已运行或快速启动时漏掉结果。
 * 若当前固件定义了 NO_ASR_FLOW，本接口始终返回 false。
 *
 * @param timeoutMs 最长等待时间（毫秒）；0 表示仅立即检查，不等待启动。
 * @return SDK 进入 CHIPINTELLI_SDK_READY 时为 true；不支持 ASR、启动失败或
 *         超时时为 false。已经成功 begin() 的实例会直接返回 true。
 * @note 等待超时不会终止共享 SDK 的后台启动任务，之后可以再次调用 begin()。
 */
bool ChipIntelliASRClass::begin(uint32_t timeoutMs) {
#if defined(NO_ASR_FLOW) && NO_ASR_FLOW
  (void)timeoutMs;
  setLastError(Error::ASRDisabled);
  return false;
#else
  taskENTER_CRITICAL();
  if (_begun) {
    _lastError = Error::None;
    taskEXIT_CRITICAL();
    return true;
  }
  taskEXIT_CRITICAL();

  QueueHandle_t queue = static_cast<QueueHandle_t>(_resultQueue);
  if (queue == nullptr) {
    queue = xQueueCreate(kQueueSize, sizeof(QueuedResult));
    if (queue == nullptr) {
      setLastError(Error::QueueAllocationFailed);
      return false;
    }
    _resultQueue = queue;
  } else {
    xQueueReset(queue);
  }

  QueueHandle_t lifecycleQueue =
      static_cast<QueueHandle_t>(_lifecycleQueue);
  if (lifecycleQueue == nullptr) {
    lifecycleQueue =
        xQueueCreate(kLifecycleQueueSize, sizeof(QueuedLifecycleEvent));
    if (lifecycleQueue == nullptr) {
      setLastError(Error::QueueAllocationFailed);
      return false;
    }
    _lifecycleQueue = lifecycleQueue;
  } else {
    xQueueReset(lifecycleQueue);
  }

  taskENTER_CRITICAL();
  _nextSequence = 0;
  _dropped = 0;
  _droppedEvents = 0;
  _accepting = true;
  _lastError = Error::None;
  taskEXIT_CRITICAL();

  // Register before starting the task so a fast or already-running shared SDK
  // cannot publish a result or lifecycle event into an unobserved window.
  chipintelli_asr_set_callback(receiveFromCore, this);
  chipintelli_asr_set_event_callback(receiveEventFromCore, this);
  if (!chipintelli_sdk_begin()) {
    end();
    setLastError(Error::SDKStartFailed);
    return false;
  }

  const uint32_t startedAt = millis();
  chipintelli_sdk_state_t state = chipintelli_sdk_state();
  while (state == CHIPINTELLI_SDK_STARTING) {
    if ((millis() - startedAt) >= timeoutMs) {
      end();
      setLastError(Error::Timeout);
      return false;
    }
    delay(1);
    state = chipintelli_sdk_state();
  }

  if (state != CHIPINTELLI_SDK_READY) {
    end();
    setLastError(Error::SDKFailed);
    return false;
  }

  taskENTER_CRITICAL();
  _begun = true;
  _lastError = Error::None;
  taskEXIT_CRITICAL();
  return true;
#endif
}

/**
 * @brief 分离 Core 结果监听器，并清空队列、丢失计数和启动状态。
 *
 * 本函数只停止 ChipIntelliASR 接收结果，不关闭可能由 ChipIntelliAudio 等
 * 模块共用的 SDK。
 */
void ChipIntelliASRClass::end() {
  taskENTER_CRITICAL();
  _accepting = false;
  _begun = false;
  _dropped = 0;
  _droppedEvents = 0;
  _lastError = Error::None;
  taskEXIT_CRITICAL();
  chipintelli_asr_set_event_callback(nullptr, nullptr);
  chipintelli_asr_set_callback(nullptr, nullptr);
  QueueHandle_t queue = static_cast<QueueHandle_t>(_resultQueue);
  if (queue != nullptr) {
    xQueueReset(queue);
  }
  QueueHandle_t lifecycleQueue =
      static_cast<QueueHandle_t>(_lifecycleQueue);
  if (lifecycleQueue != nullptr) {
    xQueueReset(lifecycleQueue);
  }
}

/**
 * @brief 注册不携带用户上下文的结果回调，并清除上下文回调。
 * @param callback 新结果回调；传入 nullptr 表示取消用户回调。
 * @note 回调由 loop() 中的 tick() 执行，不占用 SDK 消息任务。
 */
void ChipIntelliASRClass::onResult(ResultCallback callback) {
  taskENTER_CRITICAL();
  _callback = callback;
  _contextCallback = nullptr;
  _callbackContext = nullptr;
  taskEXIT_CRITICAL();
}

/**
 * @brief 注册携带用户上下文的结果回调，并清除无上下文回调。
 * @param callback 新结果回调；传入 nullptr 表示取消用户回调。
 * @param context 每次调用 callback 时原样传入的用户指针，可以为 nullptr。
 * @note 回调由 loop() 中的 tick() 执行，不占用 SDK 消息任务。
 */
void ChipIntelliASRClass::onResult(ContextCallback callback, void *context) {
  taskENTER_CRITICAL();
  _contextCallback = callback;
  _callbackContext = context;
  _callback = nullptr;
  taskEXIT_CRITICAL();
}

bool ChipIntelliASRClass::attachStartup(CommandCallback callback) {
  if (callback == nullptr) {
    setLastError(Error::InvalidCallback);
    return false;
  }
  taskENTER_CRITICAL();
  _startupCallback = callback;
  _lastError = Error::None;
  taskEXIT_CRITICAL();
  return true;
}

bool ChipIntelliASRClass::attachWakeup(CommandCallback callback) {
  if (callback == nullptr) {
    setLastError(Error::InvalidCallback);
    return false;
  }
  taskENTER_CRITICAL();
  _wakeupCallback = callback;
  _lastError = Error::None;
  taskEXIT_CRITICAL();
  return true;
}

bool ChipIntelliASRClass::attachTimeout(CommandCallback callback) {
  if (callback == nullptr) {
    setLastError(Error::InvalidCallback);
    return false;
  }
  taskENTER_CRITICAL();
  _timeoutCallback = callback;
  _lastError = Error::None;
  taskEXIT_CRITICAL();
  return true;
}

void ChipIntelliASRClass::detachStartup() {
  taskENTER_CRITICAL();
  _startupCallback = nullptr;
  taskEXIT_CRITICAL();
}

void ChipIntelliASRClass::detachWakeup() {
  taskENTER_CRITICAL();
  _wakeupCallback = nullptr;
  taskEXIT_CRITICAL();
}

void ChipIntelliASRClass::detachTimeout() {
  taskENTER_CRITICAL();
  _timeoutCallback = nullptr;
  taskEXIT_CRITICAL();
}

/**
 * @brief 清空一个命令处理器槽位。
 */
void ChipIntelliASRClass::clearHandler(EventHandler &handler) {
  handler.id = 0;
  handler.callback.command = nullptr;
  handler.context = nullptr;
  handler.type = HandlerType::Empty;
  handler.match = MatchType::Command;
}

/**
 * @brief 在按匹配类型和 ID 排序的有效区间中执行二分查找。
 */
uint16_t ChipIntelliASRClass::lowerBound(MatchType match, uint32_t id,
                                         bool &found) const {
  uint16_t first = 0;
  uint16_t last = _handlerCount;
  while (first < last) {
    const uint16_t middle =
        static_cast<uint16_t>(first + (last - first) / 2U);
    const EventHandler &candidate = _eventHandlers[middle];
    const bool candidateBefore =
        static_cast<uint8_t>(candidate.match) < static_cast<uint8_t>(match) ||
        (candidate.match == match && candidate.id < id);
    if (candidateBefore) {
      first = static_cast<uint16_t>(middle + 1U);
    } else {
      last = middle;
    }
  }
  found = first < _handlerCount && _eventHandlers[first].match == match &&
          _eventHandlers[first].id == id;
  return first;
}

/**
 * @brief 注册或替换一个命令/语义处理器。
 */
bool ChipIntelliASRClass::attachHandler(
    MatchType match, uint32_t id, HandlerType type,
    CommandCallback commandCallback,
    ResultCallback resultCallback, ContextCallback contextCallback,
    void *context) {
  if ((type == HandlerType::Command && commandCallback == nullptr) ||
      (type == HandlerType::Result && resultCallback == nullptr) ||
      (type == HandlerType::Context && contextCallback == nullptr) ||
      type == HandlerType::Empty) {
    setLastError(Error::InvalidCallback);
    return false;
  }

  EventHandler newHandler;
  clearHandler(newHandler);
  newHandler.id = id;
  newHandler.context = context;
  newHandler.type = type;
  newHandler.match = match;
  switch (type) {
  case HandlerType::Command:
    newHandler.callback.command = commandCallback;
    break;
  case HandlerType::Result:
    newHandler.callback.result = resultCallback;
    break;
  case HandlerType::Context:
    newHandler.callback.context = contextCallback;
    break;
  case HandlerType::Empty:
    break;
  }

  bool attached = false;
  taskENTER_CRITICAL();
  bool found = false;
  const uint16_t index = lowerBound(match, id, found);
  if (found) {
    _eventHandlers[index] = newHandler;
    attached = true;
  } else if (_handlerCount < kMaxEventHandlers) {
    const size_t moveCount = static_cast<size_t>(_handlerCount - index);
    if (moveCount != 0U) {
      memmove(&_eventHandlers[index + 1U], &_eventHandlers[index],
              moveCount * sizeof(EventHandler));
    }
    _eventHandlers[index] = newHandler;
    ++_handlerCount;
    attached = true;
  }
  _lastError = attached ? Error::None : Error::HandlerTableFull;
  taskEXIT_CRITICAL();
  return attached;
}

/**
 * @brief 注册不接收参数的命令处理器。
 */
bool ChipIntelliASRClass::attachCommand(uint16_t commandId,
                                        CommandCallback callback) {
  return attachHandler(MatchType::Command, commandId, HandlerType::Command,
                       callback, nullptr, nullptr, nullptr);
}

/**
 * @brief 注册接收完整识别结果的命令处理器。
 */
bool ChipIntelliASRClass::attachCommand(uint16_t commandId,
                                        ResultCallback callback) {
  return attachHandler(MatchType::Command, commandId, HandlerType::Result,
                       nullptr, callback, nullptr, nullptr);
}

/**
 * @brief 注册携带用户上下文的命令处理器。
 */
bool ChipIntelliASRClass::attachCommand(uint16_t commandId,
                                        ContextCallback callback,
                                        void *context) {
  return attachHandler(MatchType::Command, commandId, HandlerType::Context,
                       nullptr, nullptr, callback, context);
}

bool ChipIntelliASRClass::attachSemantic(uint32_t semanticId,
                                         CommandCallback callback) {
  return attachHandler(MatchType::Semantic, semanticId, HandlerType::Command,
                       callback, nullptr, nullptr, nullptr);
}

bool ChipIntelliASRClass::attachSemantic(uint32_t semanticId,
                                         ResultCallback callback) {
  return attachHandler(MatchType::Semantic, semanticId, HandlerType::Result,
                       nullptr, callback, nullptr, nullptr);
}

bool ChipIntelliASRClass::attachSemantic(uint32_t semanticId,
                                         ContextCallback callback,
                                         void *context) {
  return attachHandler(MatchType::Semantic, semanticId, HandlerType::Context,
                       nullptr, nullptr, callback, context);
}

/**
 * @brief 移除一个命令/语义 ID 的处理器。
 */
bool ChipIntelliASRClass::detachHandler(MatchType match, uint32_t id) {
  bool detached = false;
  taskENTER_CRITICAL();
  bool found = false;
  const uint16_t index = lowerBound(match, id, found);
  if (found) {
    const size_t moveCount =
        static_cast<size_t>(_handlerCount - index - 1U);
    if (moveCount != 0U) {
      memmove(&_eventHandlers[index], &_eventHandlers[index + 1U],
              moveCount * sizeof(EventHandler));
    }
    --_handlerCount;
    clearHandler(_eventHandlers[_handlerCount]);
    detached = true;
  }
  _lastError = Error::None;
  taskEXIT_CRITICAL();
  return detached;
}

bool ChipIntelliASRClass::detachCommand(uint16_t commandId) {
  return detachHandler(MatchType::Command, commandId);
}

bool ChipIntelliASRClass::detachSemantic(uint32_t semanticId) {
  return detachHandler(MatchType::Semantic, semanticId);
}

/**
 * @brief 移除指定匹配类型的全部处理器。
 */
void ChipIntelliASRClass::detachAllHandlers(MatchType match) {
  taskENTER_CRITICAL();
  bool found = false;
  const uint16_t firstSemantic = lowerBound(MatchType::Semantic, 0U, found);
  if (match == MatchType::Command) {
    const uint16_t semanticCount =
        static_cast<uint16_t>(_handlerCount - firstSemantic);
    if (semanticCount != 0U) {
      memmove(&_eventHandlers[0], &_eventHandlers[firstSemantic],
              static_cast<size_t>(semanticCount) * sizeof(EventHandler));
    }
    _handlerCount = semanticCount;
  } else {
    _handlerCount = firstSemantic;
  }
  _lastError = Error::None;
  taskEXIT_CRITICAL();
}

void ChipIntelliASRClass::detachAllCommands() {
  detachAllHandlers(MatchType::Command);
}

void ChipIntelliASRClass::detachAllSemantics() {
  detachAllHandlers(MatchType::Semantic);
}

void ChipIntelliASRClass::detachAll() {
  taskENTER_CRITICAL();
  _handlerCount = 0;
  _lastError = Error::None;
  taskEXIT_CRITICAL();
}

bool ChipIntelliASRClass::findHandler(MatchType match, uint32_t id,
                                      EventHandler &handler) const {
  bool found = false;
  const uint16_t index = lowerBound(match, id, found);
  if (found) {
    handler = _eventHandlers[index];
  }
  return found;
}

void ChipIntelliASRClass::dispatchHandler(const EventHandler &handler,
                                           const Result &result) {
  switch (handler.type) {
  case HandlerType::Command:
    handler.callback.command();
    break;
  case HandlerType::Result:
    handler.callback.result(result);
    break;
  case HandlerType::Context:
    handler.callback.context(result, handler.context);
    break;
  case HandlerType::Empty:
    break;
  }
}

bool ChipIntelliASRClass::sequenceBefore(uint32_t lhs, uint32_t rhs) {
  return static_cast<int32_t>(lhs - rhs) < 0;
}

/**
 * @brief 非阻塞地取出一条生命周期事件或结果，并在 loop 任务中派发。
 */
bool ChipIntelliASRClass::tick() {
  taskENTER_CRITICAL();
  if (_dispatching) {
    _lastError = Error::ReentrantTick;
    taskEXIT_CRITICAL();
    return false;
  }
  _dispatching = true;
  _lastError = Error::None;
  taskEXIT_CRITICAL();

  QueueHandle_t resultQueue = static_cast<QueueHandle_t>(_resultQueue);
  QueueHandle_t lifecycleQueue =
      static_cast<QueueHandle_t>(_lifecycleQueue);
  QueuedResult queuedResult;
  QueuedLifecycleEvent queuedLifecycle;
  const bool hasResult =
      resultQueue != nullptr &&
      xQueuePeek(resultQueue, &queuedResult, 0) == pdPASS;
  const bool hasLifecycle =
      lifecycleQueue != nullptr &&
      xQueuePeek(lifecycleQueue, &queuedLifecycle, 0) == pdPASS;

  if (!hasResult && !hasLifecycle) {
    taskENTER_CRITICAL();
    _dispatching = false;
    taskEXIT_CRITICAL();
    return false;
  }

  if (hasLifecycle &&
      (!hasResult ||
       sequenceBefore(queuedLifecycle.sequence, queuedResult.sequence))) {
    if (xQueueReceive(lifecycleQueue, &queuedLifecycle, 0) != pdPASS) {
      taskENTER_CRITICAL();
      _dispatching = false;
      taskEXIT_CRITICAL();
      return false;
    }

    CommandCallback lifecycleCallback = nullptr;
    taskENTER_CRITICAL();
    switch (queuedLifecycle.event) {
    case LifecycleEvent::Startup:
      lifecycleCallback = _startupCallback;
      break;
    case LifecycleEvent::Wakeup:
      lifecycleCallback = _wakeupCallback;
      break;
    case LifecycleEvent::Timeout:
      lifecycleCallback = _timeoutCallback;
      break;
    }
    taskEXIT_CRITICAL();

    if (lifecycleCallback != nullptr) {
      lifecycleCallback();
    }

    taskENTER_CRITICAL();
    _dispatching = false;
    taskEXIT_CRITICAL();
    return true;
  }

  if (xQueueReceive(resultQueue, &queuedResult, 0) != pdPASS) {
    taskENTER_CRITICAL();
    _dispatching = false;
    taskEXIT_CRITICAL();
    return false;
  }
  const Result &result = queuedResult.result;

  ResultCallback callback = nullptr;
  ContextCallback contextCallback = nullptr;
  void *callbackContext = nullptr;
  EventHandler eventHandler;
  clearHandler(eventHandler);

  taskENTER_CRITICAL();
  callback = _callback;
  contextCallback = _contextCallback;
  callbackContext = _callbackContext;
  if (!findHandler(MatchType::Command, result.commandId, eventHandler)) {
    findHandler(MatchType::Semantic, result.semanticId, eventHandler);
  }
  taskEXIT_CRITICAL();

  if (contextCallback != nullptr) {
    contextCallback(result, callbackContext);
  } else if (callback != nullptr) {
    callback(result);
  }

  dispatchHandler(eventHandler, result);

  taskENTER_CRITICAL();
  _dispatching = false;
  taskEXIT_CRITICAL();
  return true;
}

/**
 * @brief Core C ABI 回调入口，检查指针后将结果转交给对应实例。
 * @param result Core 暂时持有的识别结果；转交后会立即复制其内容。
 * @param context 注册监听器时传入的 ChipIntelliASRClass 实例指针。
 */
void ChipIntelliASRClass::receiveFromCore(
    const chipintelli_asr_result_t *result, void *context) {
  if (result == nullptr || context == nullptr) {
    return;
  }
  static_cast<ChipIntelliASRClass *>(context)->enqueue(*result);
}

void ChipIntelliASRClass::receiveEventFromCore(
    chipintelli_asr_event_type_t eventType, void *context) {
  if (context == nullptr) {
    return;
  }

  LifecycleEvent event;
  switch (eventType) {
  case CHIPINTELLI_ASR_EVENT_STARTED:
    event = LifecycleEvent::Startup;
    break;
  case CHIPINTELLI_ASR_EVENT_WAKEUP:
    event = LifecycleEvent::Wakeup;
    break;
  case CHIPINTELLI_ASR_EVENT_TIMEOUT:
    event = LifecycleEvent::Timeout;
    break;
  default:
    return;
  }
  static_cast<ChipIntelliASRClass *>(context)->enqueueLifecycle(event);
}

/**
 * @brief 复制 Core 识别结果并投递到 FreeRTOS 队列。
 *
 * text 最多复制 63 个字节并保证以 '\0' 结尾。投递的等待时间为 0，因此
 * SDK 消息任务永远不会等待 Arduino loop；队列满时只增加丢失计数。
 *
 * @param source Core 提供的识别结果，其 text 指针只在本次调用期间有效。
 */
void ChipIntelliASRClass::enqueue(const chipintelli_asr_result_t &source) {
  QueuedResult queued;
  Result &delivered = queued.result;
  delivered.commandId = source.command_id;
  delivered.semanticId = source.semantic_id;
  delivered.score = source.score;
  delivered.frames = source.frames;
  delivered.isWakeWord = source.is_wake_word;
  if (source.text != nullptr) {
    size_t boundedLength = 0;
    while (boundedLength < Result::kTextCapacity &&
           source.text[boundedLength] != '\0') {
      ++boundedLength;
    }
    const bool truncated = boundedLength == Result::kTextCapacity;
    const size_t copyLength =
        truncated ? Result::kTextCapacity - 1U : boundedLength;
    memcpy(delivered.text, source.text, copyLength);
    delivered.text[copyLength] = '\0';
    delivered.textTruncated = truncated;
  } else {
    delivered.text[0] = '\0';
    delivered.textTruncated = false;
  }

  // The zero-wait queue send is safe inside this short outer critical section.
  // Keeping the accepting check and copy atomic with end() guarantees that
  // end() really leaves an empty queue even if an SDK callback was in flight.
  taskENTER_CRITICAL();
  QueueHandle_t queue = static_cast<QueueHandle_t>(_resultQueue);
  if (_accepting && queue != nullptr) {
    queued.sequence = _nextSequence++;
    if (xQueueSend(queue, &queued, 0) != pdPASS) {
      ++_dropped;
    }
  }
  taskEXIT_CRITICAL();
}

void ChipIntelliASRClass::enqueueLifecycle(LifecycleEvent event) {
  QueuedLifecycleEvent queued;
  queued.event = event;

  taskENTER_CRITICAL();
  QueueHandle_t queue = static_cast<QueueHandle_t>(_lifecycleQueue);
  if (_accepting && queue != nullptr) {
    queued.sequence = _nextSequence++;
    if (xQueueSend(queue, &queued, 0) != pdPASS) {
      ++_droppedEvents;
    }
  }
  taskEXIT_CRITICAL();
}

/**
 * @brief 查询轮询队列中是否存在待读识别结果。
 * @return 队列非空时为 true，否则为 false。
 */
bool ChipIntelliASRClass::available() const {
  QueueHandle_t queue = static_cast<QueueHandle_t>(_resultQueue);
  return queue != nullptr && uxQueueMessagesWaiting(queue) != 0;
}

/**
 * @brief 读取并移除轮询队列中最早的一条识别结果。
 * @param result 成功时写入结果；队列为空时不会修改该对象。
 * @return 成功读取时为 true，队列为空时为 false。
 */
bool ChipIntelliASRClass::read(Result &result) {
  QueueHandle_t queue = static_cast<QueueHandle_t>(_resultQueue);
  QueuedResult queued;
  if (queue == nullptr || xQueueReceive(queue, &queued, 0) != pdPASS) {
    return false;
  }
  result = queued.result;
  return true;
}

size_t ChipIntelliASRClass::pendingResults() const {
  QueueHandle_t queue = static_cast<QueueHandle_t>(_resultQueue);
  return queue == nullptr
             ? 0U
             : static_cast<size_t>(uxQueueMessagesWaiting(queue));
}

size_t ChipIntelliASRClass::pendingEvents() const {
  QueueHandle_t queue = static_cast<QueueHandle_t>(_lifecycleQueue);
  return queue == nullptr
             ? 0U
             : static_cast<size_t>(uxQueueMessagesWaiting(queue));
}

/**
 * @brief 获取轮询队列溢出的累计次数。
 * @return 自最近一次 begin() 或 end() 清零以来丢弃的结果数量。
 */
uint32_t ChipIntelliASRClass::droppedResults() const {
  taskENTER_CRITICAL();
  const uint32_t dropped = _dropped;
  taskEXIT_CRITICAL();
  return dropped;
}

uint32_t ChipIntelliASRClass::droppedEvents() const {
  taskENTER_CRITICAL();
  const uint32_t dropped = _droppedEvents;
  taskEXIT_CRITICAL();
  return dropped;
}

size_t ChipIntelliASRClass::handlerCount() const {
  taskENTER_CRITICAL();
  const uint16_t count = _handlerCount;
  taskEXIT_CRITICAL();
  return count;
}

size_t ChipIntelliASRClass::handlerCapacity() const {
  return kMaxEventHandlers;
}

ChipIntelliASRClass::Error ChipIntelliASRClass::lastError() const {
  taskENTER_CRITICAL();
  const Error error = _lastError;
  taskEXIT_CRITICAL();
  return error;
}

void ChipIntelliASRClass::setLastError(Error error) {
  taskENTER_CRITICAL();
  _lastError = error;
  taskEXIT_CRITICAL();
}

const char *ChipIntelliASRClass::errorString(Error error) {
  switch (error) {
  case Error::None:
    return "none";
  case Error::ASRDisabled:
    return "ASR is disabled by the selected firmware profile";
  case Error::QueueAllocationFailed:
    return "ASR result queue allocation failed";
  case Error::SDKStartFailed:
    return "SDK start failed";
  case Error::SDKFailed:
    return "SDK initialization failed";
  case Error::Timeout:
    return "SDK initialization timed out";
  case Error::InvalidCallback:
    return "callback is null";
  case Error::HandlerTableFull:
    return "ASR handler table is full";
  case Error::ReentrantTick:
    return "tick cannot be called from an ASR callback";
  case Error::NotBegun:
    return "ASR has not been started";
  case Error::ControlRequestFailed:
    return "ASR control request queue is full";
  }
  return "unknown ASR error";
}

/**
 * @brief 查询 SDK 当前是否正在监听普通命令词。
 * @return SDK 已就绪且处于唤醒状态时为 true。
 */
bool ChipIntelliASRClass::isAwake() const {
  return _begun && chipintelli_asr_is_awake();
}

/**
 * @brief 用指定时长替换当前 SDK 唤醒定时器的剩余时间。
 * @param timeoutMs 从现在开始保持唤醒的毫秒数。
 * @return 已更新唤醒定时器时为 true，否则为 false。
 */
bool ChipIntelliASRClass::keepAwakeFor(uint32_t timeoutMs) {
  return _begun && chipintelli_asr_keep_awake_for(timeoutMs);
}

/**
 * @brief 异步切换唤醒词门控模式。
 * @param enabled true 要求先说唤醒词；false 直接接受普通命令。
 * @return SDK 系统消息任务已接受切换请求时为 true。
 */
bool ChipIntelliASRClass::setWakeWordEnabled(bool enabled) {
  if (!_begun) {
    setLastError(Error::NotBegun);
    return false;
  }
  if (!chipintelli_asr_set_wake_word_enabled(enabled)) {
    setLastError(Error::ControlRequestFailed);
    return false;
  }
  setLastError(Error::None);
  return true;
}

/**
 * @brief 查询编译时算法配置是否启用了声学回声消除（AEC）。
 * @return USE_AEC_MODULE 非零时为 true，否则为 false。
 */
bool ChipIntelliASRClass::isAECEnabled() const {
#if defined(USE_AEC_MODULE) && USE_AEC_MODULE
  return true;
#else
  return false;
#endif
}

/**
 * @brief 查询当前编译配置是否允许识别结果打断正在播放的提示音。
 * @return bargeInMode() 不是 Disabled 时为 true。
 */
bool ChipIntelliASRClass::isBargeInEnabled() const {
  return bargeInMode() != BargeInMode::Disabled;
}

/**
 * @brief 根据 USE_AEC_MODULE 和 AEC_INTERRUPT_TYPE 返回语音打断模式。
 * @return 未启用 AEC 时为 Disabled；AEC_INTERRUPT_TYPE 为 0、1、2 时分别
 *         对应 WakeWordOnly、CommandOnly、WakeWordAndCommand。
 */
ChipIntelliASRClass::BargeInMode ChipIntelliASRClass::bargeInMode() const {
#if !defined(USE_AEC_MODULE) || !USE_AEC_MODULE
  return BargeInMode::Disabled;
#elif AEC_INTERRUPT_TYPE == 0
  return BargeInMode::WakeWordOnly;
#elif AEC_INTERRUPT_TYPE == 1
  return BargeInMode::CommandOnly;
#else
  return BargeInMode::WakeWordAndCommand;
#endif
}
