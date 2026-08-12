#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#define CHIPINTELLI_LANGUAGE_ZH 0
#define CHIPINTELLI_LANGUAGE_EN 1
#define CHIPINTELLI_LANGUAGE_JA 2
#define CHIPINTELLI_LANGUAGE_KO 3
#define CHIPINTELLI_LANGUAGE_RU 4
#define CHIPINTELLI_LANGUAGE_ES 5
#define CHIPINTELLI_LANGUAGE_TH 6
#define CHIPINTELLI_LANGUAGE_DE 7
#define CHIPINTELLI_LANGUAGE_ID 8
#define CHIPINTELLI_LANGUAGE_VI 9
#define CHIPINTELLI_LANGUAGE_FR 10
#define CHIPINTELLI_LANGUAGE_PT 11
#define CHIPINTELLI_LANGUAGE_FA 12
#define CHIPINTELLI_LANGUAGE_TR 13
#define CHIPINTELLI_LANGUAGE_AR 14

#ifndef CHIPINTELLI_LANGUAGE
#define CHIPINTELLI_LANGUAGE CHIPINTELLI_LANGUAGE_ZH
#endif

#if CHIPINTELLI_LANGUAGE < CHIPINTELLI_LANGUAGE_ZH || \
    CHIPINTELLI_LANGUAGE > CHIPINTELLI_LANGUAGE_AR
#error "Unsupported CHIPINTELLI_LANGUAGE value"
#endif

extern "C" void chipintelli_sdk_prompt_unlocked(void);

class ChipIntelliAudioFactory;

class ChipIntelliAudioClass {
public:
  using FinishedCallback = void (*)(void *context);

  enum class NumberLanguage : uint8_t {
    Chinese = CHIPINTELLI_LANGUAGE_ZH,
    English = CHIPINTELLI_LANGUAGE_EN,
    Japanese = CHIPINTELLI_LANGUAGE_JA,
    Korean = CHIPINTELLI_LANGUAGE_KO,
    Russian = CHIPINTELLI_LANGUAGE_RU,
    Spanish = CHIPINTELLI_LANGUAGE_ES,
    Thai = CHIPINTELLI_LANGUAGE_TH,
    German = CHIPINTELLI_LANGUAGE_DE,
    Indonesian = CHIPINTELLI_LANGUAGE_ID,
    Vietnamese = CHIPINTELLI_LANGUAGE_VI,
    French = CHIPINTELLI_LANGUAGE_FR,
    Portuguese = CHIPINTELLI_LANGUAGE_PT,
    Persian = CHIPINTELLI_LANGUAGE_FA,
    Turkish = CHIPINTELLI_LANGUAGE_TR,
    Arabic = CHIPINTELLI_LANGUAGE_AR,
  };

  // Voice IDs for the reusable Mandarin number tokens in voice.bin.
  // digits[0] through digits[9] are 零 through 九.
  struct NumberVoiceIds {
    uint16_t digits[10];
    uint16_t ten;
    uint16_t hundred;
    uint16_t thousand;
    uint16_t tenThousand;
    uint16_t hundredMillion;
    uint16_t negative;
    uint16_t decimalPoint;
  };

  // The asynchronous playback task plays these IDs as one logical prompt and
  // delivers one completion callback for the complete sequence.
  static constexpr size_t kMaxVoiceSequenceLength = 24U;

  ChipIntelliAudioClass(const ChipIntelliAudioClass &) = delete;
  ChipIntelliAudioClass &operator=(const ChipIntelliAudioClass &) = delete;

  // Starts the shared vendor SDK when necessary and waits up to 10 seconds for
  // the flash resources, codec, amplifier and audio tasks to become ready.
  // Returns false when startup fails or times out.
  bool begin();
  void end();

  /**
   * @brief 直接按语音 ID 播放 voice.bin 中的一条语音资源。
   * @param voiceId voice.bin 中的 16 位语音资源 ID。
   * @param interruptCurrent true：中断当前提示音；false：不打断并排队播放。
   * @return 异步播放队列接受请求时为 true；未初始化或队列已满时为 false。
   * @note 返回 true 只表示请求已入队，不保证资源存在或最终播放成功。
   */
  bool playVoice(uint16_t voiceId, bool interruptCurrent = true);

  /**
   * @brief Parse and play a localized decimal number using voice IDs from 300.
   *
   * Accepts an optional leading sign, an optional decimal point, and ASCII
   * whitespace around the value. Examples: "300", "-1.5", and "0.02".
   * The integer part must fit uint32_t and the composed prompt must not exceed
   * kMaxVoiceSequenceLength. Scientific notation is not accepted.
   * @param numberText Decimal number to parse and speak.
   * @param interruptCurrent Whether to interrupt the prompt that is playing.
   *        This only affects the start of the composed number; all subsequent
   *        clips in that number play continuously.
   * @return true when the asynchronous playback queue accepts the sequence.
   */
  bool playVoice(const String &numberText, bool interruptCurrent = true) {
    return playLocalizedNumber(
        numberText,
        static_cast<NumberLanguage>(CHIPINTELLI_LANGUAGE),
        interruptCurrent);
  }

  /**
   * @brief Play a copied sequence of voice IDs as one uninterrupted prompt.
   * @param voiceIds Voice IDs from the current voice.bin.
   * @param count Number of IDs; valid range is 1..kMaxVoiceSequenceLength.
   * @param interruptCurrent Whether to interrupt the prompt that is playing.
   *        This only controls how the complete sequence starts; clips inside
   *        the sequence are always played continuously.
   * @return true when the asynchronous playback queue accepts the sequence.
   * @note Emits one finished callback after the whole sequence is processed.
   */
  bool playVoiceSequence(const uint16_t *voiceIds, size_t count,
                         bool interruptCurrent = true);

  /**
   * Build the Mandarin reading of a signed 32-bit integer as voice IDs.
   * Returns the ID count, or zero when output is null or capacity is too small.
   */
  size_t buildNumberVoiceSequence(int32_t value,
                                  const NumberVoiceIds &voiceIds,
                                  uint16_t *output,
                                  size_t capacity) const;

  /**
   * Build a fixed-point Mandarin number. For example, value=235 and
   * fractionalDigits=1 produces 二十三点五. Trailing fractional zeroes are
   * preserved. fractionalDigits must be in the range 0..9. Returns zero for
   * invalid arguments or an undersized output buffer.
   */
  size_t buildFixedPointVoiceSequence(int32_t value,
                                      uint8_t fractionalDigits,
                                      const NumberVoiceIds &voiceIds,
                                      uint16_t *output,
                                      size_t capacity) const;

  /** Build and play a signed integer using the supplied reusable tokens. */
  bool playNumber(int32_t value, const NumberVoiceIds &voiceIds);

  /** Build and play a deterministic fixed-point value. */
  bool playFixedPoint(int32_t value, uint8_t fractionalDigits,
                      const NumberVoiceIds &voiceIds);

  /**
   * @brief 连续播放资源包内置的“滴”提示音。
   * @param count 播放次数，取值范围为 1～16。
   * @return 整组请求成功入队时为 true；参数无效、未初始化或队列已满时为 false。
   * @note 本接口会中断当前提示音；整组播放完成后只触发一次完成回调。
   */
  bool playBeep(unsigned int count = 1);

  /** @brief 使用 unsigned long 命令 ID 播放；先验证其是否在 16 位范围内。 */
  bool playCommand(unsigned long commandId, int optionIndex = -1,
                   bool interruptCurrent = true);

  /** @brief 使用 long 命令 ID 播放；先验证其非负且在 16 位范围内。 */
  bool playCommand(long commandId, int optionIndex = -1,
                   bool interruptCurrent = true);

  /** @brief 使用 unsigned int 命令 ID 播放；先验证其是否在 16 位范围内。 */
  bool playCommand(unsigned int commandId, int optionIndex = -1,
                   bool interruptCurrent = true);

  /** @brief 使用 int 命令 ID 播放；先验证其非负且在 16 位范围内。 */
  bool playCommand(int commandId, int optionIndex = -1,
                   bool interruptCurrent = true);

  /**
   * @brief 按命令 ID 查找命令词表，选择该命令关联的提示音并播放。
   * @param commandId 命令词表中的 16 位命令 ID，不是语音资源 ID。
   * @param optionIndex 从 0 开始的播报选项索引；-1 表示由资源配置选择。
   * @param interruptCurrent true：中断当前提示音；false：不打断并排队播放。
   * @return 请求成功入队时为 true；未初始化或队列已满时为 false。
   */
  bool playCommand(uint16_t commandId, int optionIndex = -1,
                   bool interruptCurrent = true);
  bool playSemantic(uint32_t semanticId, int optionIndex = -1,
                    bool interruptCurrent = true);

  /**
   * @brief 按已配置的命令文本查找提示音并播放；本函数不是 TTS 接口。
   * @param commandText 资源包中的命令字符串，不能为 nullptr 或空字符串。
   * @param optionIndex 从 0 开始的播报选项索引；-1 表示由资源配置选择。
   * @param interruptCurrent true：中断当前提示音；false：不打断并排队播放。
   * @return 请求成功入队时为 true；参数无效、未初始化或队列已满时为 false。
   */
  bool playCommand(const char *commandText, int optionIndex = -1,
                   bool interruptCurrent = true);

  // Enqueues a stop request without waiting for the SDK player to become idle.
  bool stop();
  bool isPlaying() const;
  bool isReady() const;

  void setVolume(uint8_t percent);
  uint8_t volume() const;

  // The vendor SDK's audio_play_set_mute() is a no-op in SDK V2.7.14.
  // This wrapper implements reliable mute by preserving the requested volume
  // and applying zero gain until unmuted.
  void setMuted(bool muted);
  void mute();
  void unmute();
  bool isMuted() const;

  // Callback runs from the Arduino event dispatcher after the SDK releases
  // its prompt mutex. Pass nullptr to clear the callback.
  void onFinished(FinishedCallback callback, void *context = nullptr);
  uint32_t droppedFinishedCallbacks() const;

private:
  struct PlaybackRequest;

  friend class ChipIntelliAudioFactory;
  friend void chipintelli_sdk_prompt_unlocked(void);

  ChipIntelliAudioClass();

  static void playbackTaskEntry(void *context);
  static void sdkPlaybackFinished0(void *commandHandle);
  static void sdkPlaybackFinished1(void *commandHandle);
  static void finishedEvent(void *context, uint32_t value);
  void playbackTaskLoop();
  bool ensurePlaybackTask();
  bool enqueuePlaybackRequest(const PlaybackRequest &request);
  bool startPlaybackRequest(const PlaybackRequest &request,
                            bool interruptCurrent);
  void finishLogicalPlayback();
  void recordSdkPlaybackFinished(uint8_t callbackSlot);
  void dispatchFinishedCallbacks();
  bool playLocalizedNumber(const String &numberText,
                           NumberLanguage language,
                           bool interruptCurrent);

  FinishedCallback _finishedCallback;
  void *_finishedContext;
  uint32_t _finishedGeneration;
  bool _begun;
  bool _muted;
  uint8_t _unmutedVolume;
  void *_playbackQueue;
  void *_interruptQueue;
  void *_playbackTask;
  uint32_t _sdkFinished[2];
  uint8_t _activeCallbackSlot;
  bool _playbackActive;
  volatile uint32_t _droppedFinishedCallbacks;
};

extern ChipIntelliAudioClass &ChipIntelliAudio;
