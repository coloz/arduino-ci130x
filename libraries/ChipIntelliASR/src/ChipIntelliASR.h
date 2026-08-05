#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Arduino.h owns the stable C ABI between the core's SDK hook and this
// library: chipintelli_asr_result_t, chipintelli_asr_callback_t, and
// chipintelli_asr_set_callback(). The core owns the pointed-to text; this
// library copies it before returning from the hook.

/**
 * @brief 一次离线语音识别结果的 Arduino 侧副本。
 *
 * SDK 提供的命令文本会被复制到本结构体，因此回调返回后仍可安全使用。
 */
struct ChipIntelliASRResult {
  /// text 数组总容量，包含末尾的 '\0'，所以最多保存 63 个文本字节。
  static constexpr size_t kTextCapacity = 64;

  /// 命令词表中的 16 位命令 ID。
  uint16_t commandId;
  /// 命令词配置的 32 位语义 ID。
  uint32_t semanticId;
  /// SDK 给出的识别分数；部分结果（例如 CWSL）不提供分数，此时为 0。
  int16_t score;
  /// 本次识别使用的帧数；SDK 未提供该信息时为 0。
  uint16_t frames;
  /// 本结果来自唤醒词时为 true，来自普通命令词时为 false。
  bool isWakeWord;
  /// 以 '\0' 结尾的命令文本副本，最多保存 63 个字节。
  char text[kTextCapacity];
  /// 原始命令文本因超过 text 容量而被截断时为 true。
  bool textTruncated;
};

class ChipIntelliASRClass {
public:
  using Result = ChipIntelliASRResult;

  /// 不接收参数的命令事件回调类型，适合最简的 Arduino 处理函数。
  using CommandCallback = void (*)();

  /// 接收完整识别结果的事件回调类型。
  using ResultCallback = void (*)(const Result &result);

  /// 携带用户 context 指针的识别结果回调类型。
  using ContextCallback = void (*)(const Result &result, void *context);

  /** @brief 当前固件算法配置允许哪类识别结果打断正在播放的提示音。 */
  enum class BargeInMode : uint8_t {
    Disabled = 0,       ///< 未启用 AEC，播放期间不能进行语音打断。
    WakeWordOnly,       ///< 仅唤醒词可以打断提示音。
    CommandOnly,        ///< 仅普通命令词可以打断提示音。
    WakeWordAndCommand, ///< 唤醒词和普通命令词都可以打断提示音。
  };

  /** @brief 可由 lastError() 查询的最近一次库操作状态。 */
  enum class Error : uint8_t {
    None = 0,
    ASRDisabled,
    QueueAllocationFailed,
    SDKStartFailed,
    SDKFailed,
    Timeout,
    InvalidCallback,
    HandlerTableFull,
    ReentrantTick,
  };

  /** @brief 返回唯一的 ASR 硬件实例。通常直接使用全局 ChipIntelliASR。 */
  static ChipIntelliASRClass &instance();

  ChipIntelliASRClass(const ChipIntelliASRClass &) = delete;
  ChipIntelliASRClass &operator=(const ChipIntelliASRClass &) = delete;
  ChipIntelliASRClass(ChipIntelliASRClass &&) = delete;
  ChipIntelliASRClass &operator=(ChipIntelliASRClass &&) = delete;

  /**
   * @brief 启动或接入共享 SDK，并等待 ASR 和音频输入通路就绪。
   * @param timeoutMs 最长等待时间，单位为毫秒，默认为 10000；0 只检查一次
   *                  当前状态，不等待异步初始化完成。
   * @return ASR 已就绪时返回 true；固件未包含 ASR、SDK 启动失败或等待超时
   *         时返回 false。重复调用已经成功启动的实例会直接返回 true。
   * @note 超时只会分离本实例的监听器，不会停止仍在启动的共享 SDK 任务。
   */
  bool begin(uint32_t timeoutMs = 10000U);

  /**
   * @brief 停止本实例接收结果，并清空结果队列和丢失计数。
   * @note 共享 SDK 不会被关闭，因为音频等其他服务可能仍在使用它。
   */
  void end();

  /**
   * @brief 注册接收所有识别结果的事件回调。
   * @param callback tick() 取到一条新结果时调用的函数；传入 nullptr 可取消。
   * @note 回调由 loop() 中的 tick() 执行，不会在 CI13XX SDK 任务中执行。
   */
  void onResult(ResultCallback callback);

  /**
   * @brief 注册携带用户上下文的识别结果回调。
   * @param callback tick() 取到一条新结果时调用的函数；传入 nullptr 可取消。
   * @param context 原样传给 callback 的用户指针，可以为 nullptr。
   * @note 两个 onResult() 重载互斥，后一次注册会替换前一种回调。
   */
  void onResult(ContextCallback callback, void *context);

  /**
   * @brief 注册 SDK 启动完成事件。
   * @param callback 由 tick() 在 Arduino loop 任务中调用；不能为 nullptr。
   * @return 注册成功时为 true，callback 为空时为 false。
   * @note 可以在 begin() 前后注册；启动事件会先进入 RTOS 队列，不在 SDK
   *       初始化任务中直接执行用户代码。
   */
  bool attachStartup(CommandCallback callback);

  /**
   * @brief 注册识别到唤醒词并进入命令会话后的事件。
   * @param callback 由 tick() 在 Arduino loop 任务中调用；不能为 nullptr。
   * @return 注册成功时为 true，callback 为空时为 false。
   */
  bool attachWakeup(CommandCallback callback);

  /**
   * @brief 注册唤醒窗口超时并返回仅监听唤醒词状态后的事件。
   * @param callback 由 tick() 在 Arduino loop 任务中调用；不能为 nullptr。
   * @return 注册成功时为 true，callback 为空时为 false。
   */
  bool attachTimeout(CommandCallback callback);

  /** @brief 取消 SDK 启动完成事件处理函数。 */
  void detachStartup();

  /** @brief 取消进入唤醒状态事件处理函数。 */
  void detachWakeup();

  /** @brief 取消唤醒超时事件处理函数。 */
  void detachTimeout();

  /**
   * @brief 为一个命令 ID 注册无参数事件处理函数。
   * @param commandId cmd_info 命令词表中的 16 位命令 ID。
   * @param callback 匹配结果由 tick() 派发时调用的函数，不能为 nullptr。
   * @return 注册成功时为 true；callback 为空或处理器表已满时为 false。
   * @note 同一 commandId 再次注册会替换旧处理函数。唤醒词也有命令 ID，
   *       因此可以用本接口注册唤醒词处理函数。
   */
  bool attachCommand(uint16_t commandId, CommandCallback callback);

  /**
   * @brief 为一个命令 ID 注册接收完整结果的事件处理函数。
   * @param commandId cmd_info 命令词表中的 16 位命令 ID。
   * @param callback 匹配结果由 tick() 派发时调用的函数，不能为 nullptr。
   * @return 注册成功时为 true；callback 为空或处理器表已满时为 false。
   */
  bool attachCommand(uint16_t commandId, ResultCallback callback);

  /**
   * @brief 为一个命令 ID 注册携带用户上下文的事件处理函数。
   * @param commandId cmd_info 命令词表中的 16 位命令 ID。
   * @param callback 匹配结果由 tick() 派发时调用的函数，不能为 nullptr。
   * @param context 原样传给 callback 的用户指针，可以为 nullptr。
   * @return 注册成功时为 true；callback 为空或处理器表已满时为 false。
   */
  bool attachCommand(uint16_t commandId, ContextCallback callback,
                     void *context);

  /** @brief 为一个 32 位语义 ID 注册无参数事件处理函数。 */
  bool attachSemantic(uint32_t semanticId, CommandCallback callback);

  /** @brief 为一个 32 位语义 ID 注册接收完整结果的事件处理函数。 */
  bool attachSemantic(uint32_t semanticId, ResultCallback callback);

  /** @brief 为一个 32 位语义 ID 注册携带用户上下文的事件处理函数。 */
  bool attachSemantic(uint32_t semanticId, ContextCallback callback,
                      void *context);

  /**
   * @brief 移除一个命令 ID 的事件处理函数。
   * @return 找到并移除处理函数时为 true；未注册该 ID 时为 false。
   */
  bool detachCommand(uint16_t commandId);

  /** @brief 移除所有通过 attachCommand() 注册的处理函数。 */
  void detachAllCommands();

  /** @brief 移除一个语义 ID 的事件处理函数。 */
  bool detachSemantic(uint32_t semanticId);

  /** @brief 移除所有通过 attachSemantic() 注册的处理函数。 */
  void detachAllSemantics();

  /** @brief 同时移除全部命令和语义事件处理函数。 */
  void detachAll();

  /**
   * @brief 非阻塞地取出并派发一条生命周期事件或识别结果。
   * @return 取出一条事件或结果时为 true；当前无待处理内容时立即返回 false。
   * @note 应在每次 loop() 中调用。事件按 SDK 产生顺序派发；识别结果先调用
   *       onResult()，再优先匹配 commandId，没有精确匹配时回退到 semanticId。
   *       read() 只消费识别结果，使用生命周期事件时应通过 tick() 派发。
   */
  bool tick();

  /**
   * @brief 查询轮询队列中是否至少有一条待读结果。
   * @return 队列非空时为 true，否则为 false。
   */
  bool available() const;

  /**
   * @brief 从轮询队列读取并移除最早的一条识别结果。
   * @param result 成功时接收识别结果；队列为空时保持不变。
   * @return 成功读取一条结果时为 true，队列为空时为 false。
   */
  bool read(Result &result);

  /** @brief 返回当前等待 tick()/read() 处理的结果数量。 */
  size_t pendingResults() const;

  /** @brief 返回当前等待 tick() 派发的生命周期事件数量。 */
  size_t pendingEvents() const;

  /**
   * @brief 获取因轮询队列已满而丢弃的结果数量。
   * @return 自最近一次 begin() 或 end() 清零以来的累计丢弃数量。
   * @note 事件回调与 read() 共用队列；队列已满时该条结果无法被 tick() 派发。
   */
  uint32_t droppedResults() const;

  /** @brief 返回因生命周期事件队列已满而丢弃的累计事件数量。 */
  uint32_t droppedEvents() const;

  /** @brief 返回当前已注册的命令与语义处理器总数。 */
  size_t handlerCount() const;

  /** @brief 返回命令与语义处理器共享表的最大容量。 */
  size_t handlerCapacity() const;

  /** @brief 返回最近一次 begin()/attach()/tick() 失败的具体原因。 */
  Error lastError() const;

  /** @brief 返回不分配内存的静态错误说明字符串。 */
  static const char *errorString(Error error);

  /**
   * @brief 查询 SDK 当前是否处于命令会话的唤醒状态。
   * @return 已唤醒且 SDK 就绪时为 true；仅监听唤醒词或未就绪时为 false。
   * @note 无 AEC 配置在播报期间可能暂时暂停识别，但不会改变本会话状态。
   */
  bool isAwake() const;

  /**
   * @brief 从调用时刻起重新设置当前唤醒状态的剩余时间。
   * @param timeoutMs 保持唤醒的毫秒数，必须大于 0。
   * @return 当前确实处于唤醒状态且定时器已更新时为 true，否则为 false。
   * @note 本函数不会从仅监听唤醒词状态强制唤醒，适合在 onResult() 中为每条
   *       新结果刷新连续命令窗口。
   */
  bool keepAwakeFor(uint32_t timeoutMs);

  /**
   * @brief 查询编译时选择的算法配置是否启用了声学回声消除（AEC）。
   * @return 启用 AEC 时为 true，否则为 false。
   */
  bool isAECEnabled() const;

  /**
   * @brief 查询当前编译配置是否支持语音打断正在播放的提示音。
   * @return bargeInMode() 不是 Disabled 时为 true。
   */
  bool isBargeInEnabled() const;

  /**
   * @brief 获取编译时配置的提示音语音打断模式。
   * @return Disabled、WakeWordOnly、CommandOnly 或 WakeWordAndCommand。
   * @note 打断由 SDK ASR 流程自动完成，不需要在结果回调中调用音频停止接口。
   */
  BargeInMode bargeInMode() const;

private:
  static constexpr uint8_t kQueueSize = 8;
  static constexpr uint8_t kLifecycleQueueSize = 8;
  // CI1302 officially supports up to 300 command words and CI1303 up to 500.
  // Command and semantic bindings share this deterministic, heap-free table.
  static constexpr uint16_t kMaxEventHandlers = 512;

  enum class MatchType : uint8_t {
    Command = 0,
    Semantic,
  };

  enum class HandlerType : uint8_t {
    Empty = 0,
    Command,
    Result,
    Context,
  };

  union HandlerCallback {
    CommandCallback command;
    ResultCallback result;
    ContextCallback context;
  };

  struct EventHandler {
    uint32_t id;
    HandlerCallback callback;
    void *context;
    HandlerType type;
    MatchType match;
  };

  enum class LifecycleEvent : uint8_t {
    Startup = 0,
    Wakeup,
    Timeout,
  };

  struct QueuedResult {
    uint32_t sequence;
    Result result;
  };

  struct QueuedLifecycleEvent {
    uint32_t sequence;
    LifecycleEvent event;
  };

  ChipIntelliASRClass();

  /**
   * @brief 将 Arduino Core 的 C ABI 结果回调转发给对应的类实例。
   * @param result Core 提供的临时结果指针。
   * @param context 注册回调时传入的 ChipIntelliASRClass 实例指针。
   */
  static void receiveFromCore(const chipintelli_asr_result_t *result,
                              void *context);

  /** @brief 将 Arduino Core 的生命周期事件转交给对应类实例。 */
  static void receiveEventFromCore(chipintelli_asr_event_type_t eventType,
                                   void *context);

  /**
   * @brief 复制一条 Core 结果并非阻塞地投递到 RTOS 队列。
   * @param result Core 提供的识别结果；其中的文本在本函数内完成复制。
   */
  void enqueue(const chipintelli_asr_result_t &result);
  void enqueueLifecycle(LifecycleEvent event);

  bool attachHandler(MatchType match, uint32_t id, HandlerType type,
                     CommandCallback commandCallback,
                     ResultCallback resultCallback,
                     ContextCallback contextCallback, void *context);
  bool detachHandler(MatchType match, uint32_t id);
  void detachAllHandlers(MatchType match);
  uint16_t lowerBound(MatchType match, uint32_t id, bool &found) const;
  bool findHandler(MatchType match, uint32_t id,
                   EventHandler &handler) const;
  static void clearHandler(EventHandler &handler);
  static void dispatchHandler(const EventHandler &handler,
                              const Result &result);
  static bool sequenceBefore(uint32_t lhs, uint32_t rhs);
  void setLastError(Error error);

  static ChipIntelliASRClass _instance;
  void *_resultQueue;
  void *_lifecycleQueue;
  uint32_t _nextSequence;
  volatile uint32_t _dropped;
  volatile uint32_t _droppedEvents;
  ResultCallback _callback;
  ContextCallback _contextCallback;
  void *_callbackContext;
  CommandCallback _startupCallback;
  CommandCallback _wakeupCallback;
  CommandCallback _timeoutCallback;
  EventHandler _eventHandlers[kMaxEventHandlers];
  uint16_t _handlerCount;
  Error _lastError;
  bool _begun;
  bool _dispatching;
  volatile bool _accepting;
};

extern ChipIntelliASRClass &ChipIntelliASR;
