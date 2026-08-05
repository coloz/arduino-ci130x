#pragma once

#include <Arduino.h>
#include <stdint.h>

extern "C" void chipintelli_sdk_prompt_unlocked(void);

class ChipIntelliAudioFactory;

class ChipIntelliAudioClass {
public:
  using FinishedCallback = void (*)(void *context);

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
   * @return SDK 接受请求时为 true；未初始化或请求被立即拒绝时为 false。
   * @note 返回 true 只表示请求已接受，不保证资源存在或最终播放成功。
   */
  bool playVoice(uint16_t voiceId, bool interruptCurrent = true);

  /**
   * @brief 连续播放资源包内置的“滴”提示音。
   * @param count 播放次数，取值范围为 1～16。
   * @return 整组播放请求被接受时为 true；参数无效、未初始化、资源不存在或
   *         请求被拒绝时为 false。
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
   * @return SDK 接受请求时为 true；未初始化、命令不存在或请求被拒绝时为 false。
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
   * @return SDK 接受请求时为 true；参数无效、命令不存在或请求被拒绝时为 false。
   */
  bool playCommand(const char *commandText, int optionIndex = -1,
                   bool interruptCurrent = true);

  // Requests a stop. The SDK performs a bounded internal wait but does not
  // expose whether playback reached idle before that wait expired.
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

  // Callback runs only after the SDK releases its prompt mutex. Keep it short
  // and hand longer work back to loop(). Pass nullptr to clear the callback.
  void onFinished(FinishedCallback callback, void *context = nullptr);

private:
  friend class ChipIntelliAudioFactory;
  friend void chipintelli_sdk_prompt_unlocked(void);

  ChipIntelliAudioClass();

  static void sdkPlaybackFinished(void *commandHandle);
  void dispatchFinishedCallbacks();
  bool hasFinishedCallback() const;

  FinishedCallback _finishedCallback;
  void *_finishedContext;
  bool _begun;
  bool _muted;
  uint8_t _unmutedVolume;
  uint32_t _pendingFinished;
  bool _dispatchingFinished;
};

extern ChipIntelliAudioClass &ChipIntelliAudio;
