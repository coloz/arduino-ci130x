#include "ChipIntelliAudio.h"

extern "C" {
#include "FreeRTOS.h"
#include "audio_play_api.h"
#include "ci_flash_data_info.h"
#include "prompt_player.h"
#include "system_msg_deal.h"
#include "task.h"

uint32_t ci_arduino_audio_started_message_count(void);
uint32_t ci_arduino_prompt_play_voice_sequence(
    const uint16_t *voiceIds, uint8_t count,
    play_done_callback_t playDoneCallback, bool preemptive);
}

namespace {
constexpr uint32_t kInitTimeoutMs = 10000;
constexpr uint32_t kIdleStabilityMs = 500;
constexpr uint32_t kPowersOfTen[] = {
    1U,          10U,        100U,       1000U,      10000U,
    100000U,     1000000U,   10000000U,  100000000U, 1000000000U,
};
constexpr ChipIntelliAudioClass::NumberVoiceIds kDefaultNumberVoiceIds = {
    {300U, 301U, 302U, 303U, 304U, 305U, 306U, 307U, 308U, 309U},
    310U,  // 十
    311U,  // 百
    312U,  // 千
    313U,  // 万
    314U,  // 亿
    315U,  // 负
    316U,  // 点
};

bool isAsciiWhitespace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
         value == '\f' || value == '\v';
}

class VoiceSequenceBuilder {
 public:
  VoiceSequenceBuilder(uint16_t *output, size_t capacity)
      : _output(output), _capacity(capacity), _count(0U), _valid(true) {}

  bool append(uint16_t voiceId) {
    if (!_valid || _output == nullptr || _count >= _capacity) {
      _valid = false;
      return false;
    }
    _output[_count++] = voiceId;
    return true;
  }

  size_t size() const { return _valid ? _count : 0U; }

 private:
  uint16_t *_output;
  size_t _capacity;
  size_t _count;
  bool _valid;
};

void appendNumberGroup(uint16_t group, bool omitLeadingOne,
                       const ChipIntelliAudioClass::NumberVoiceIds &voiceIds,
                       VoiceSequenceBuilder &output) {
  const uint16_t placeValues[] = {
      0U, voiceIds.ten, voiceIds.hundred, voiceIds.thousand,
  };
  const uint16_t divisors[] = {1000U, 100U, 10U, 1U};
  bool emittedDigit = false;
  bool pendingZero = false;

  for (size_t index = 0U; index < 4U; ++index) {
    const uint16_t divisor = divisors[index];
    const uint8_t digit = static_cast<uint8_t>((group / divisor) % 10U);
    const uint8_t position = static_cast<uint8_t>(3U - index);

    if (digit == 0U) {
      if (emittedDigit) {
        pendingZero = true;
      }
      continue;
    }

    if (pendingZero) {
      output.append(voiceIds.digits[0]);
      pendingZero = false;
    }

    const bool omitDigit = omitLeadingOne && !emittedDigit &&
                           position == 1U && digit == 1U;
    if (!omitDigit) {
      output.append(voiceIds.digits[digit]);
    }
    if (position != 0U) {
      output.append(placeValues[position]);
    }
    emittedDigit = true;
  }
}

void appendUnsignedNumber(
    uint32_t value,
    const ChipIntelliAudioClass::NumberVoiceIds &voiceIds,
    VoiceSequenceBuilder &output) {
  if (value == 0U) {
    output.append(voiceIds.digits[0]);
    return;
  }

  const uint16_t groups[] = {
      static_cast<uint16_t>(value % 10000U),
      static_cast<uint16_t>((value / 10000U) % 10000U),
      static_cast<uint16_t>(value / 100000000U),
  };
  bool emittedGroup = false;
  bool pendingZero = false;

  for (int index = 2; index >= 0; --index) {
    const uint16_t group = groups[index];
    if (group == 0U) {
      if (emittedGroup) {
        pendingZero = true;
      }
      continue;
    }

    const bool highestGroup = !emittedGroup;
    if (emittedGroup && (pendingZero || group < 1000U)) {
      output.append(voiceIds.digits[0]);
    }

    appendNumberGroup(group, highestGroup, voiceIds, output);
    if (index == 2) {
      output.append(voiceIds.hundredMillion);
    } else if (index == 1) {
      output.append(voiceIds.tenThousand);
    }
    emittedGroup = true;
    pendingZero = false;
  }
}

uint32_t signedMagnitude(int32_t value) {
  const uint32_t unsignedValue = static_cast<uint32_t>(value);
  return value < 0 ? 0U - unsignedValue : unsignedValue;
}

bool sdkAudioReady() {
  bool flashReady = false;
  is_ci_flash_data_info_inited(&flashReady);

  // vol_get() starts at VOLUME_MAX + 1. It enters the configured range only
  // after the SDK system task has handled SYS_MSG_TYPE_AUDIO_IN_STARTED,
  // initialized the persisted volume and enabled the board audio path.
  uint8_t volumeLevel = vol_get();
  if (flashReady && audio_play_task_handle != nullptr &&
      ci_arduino_audio_started_message_count() != 0U &&
      (volumeLevel < VOLUME_MIN || volumeLevel > VOLUME_MAX)) {
    // The vendor startup path leaves its VOLUME_MAX + 1 sentinel unchanged if
    // NVDM contains an out-of-range byte. Repair it once the audio-start
    // message has been consumed instead of waiting for begin() to time out.
    volumeLevel = vol_set(VOLUME_DEFAULT);
  }
  return flashReady && audio_play_task_handle != nullptr &&
         volumeLevel >= VOLUME_MIN && volumeLevel <= VOLUME_MAX;
}
}  // namespace

class ChipIntelliAudioFactory {
 public:
  static ChipIntelliAudioClass &instance() {
    static ChipIntelliAudioClass instance;
    return instance;
  }
};

ChipIntelliAudioClass &ChipIntelliAudio =
    ChipIntelliAudioFactory::instance();

extern "C" int chipintelli_audio_mute_requested(void) {
  return ChipIntelliAudio.isMuted() ? 1 : 0;
}

extern "C" void chipintelli_sdk_prompt_unlocked(void) {
  ChipIntelliAudio.dispatchFinishedCallbacks();
}

ChipIntelliAudioClass::ChipIntelliAudioClass()
    : _finishedCallback(nullptr),
      _finishedContext(nullptr),
      _begun(false),
      _muted(false),
      _unmutedVolume(70),
      _pendingFinished(0),
      _dispatchingFinished(false) {}

bool ChipIntelliAudioClass::begin() {
  if (_begun) {
    return true;
  }
  if (!chipintelli_sdk_begin()) {
    return false;
  }

  const uint32_t started = millis();
  uint32_t idleSince = 0;
  while (true) {
    if (!sdkAudioReady() || prompt_is_playing() != 0U) {
      idleSince = 0;
    } else if (idleSince == 0) {
      idleSince = millis();
    } else if ((millis() - idleSince) >= kIdleStabilityMs) {
      break;
    }

    if ((millis() - started) >= kInitTimeoutMs) {
      return false;
    }
    delay(1);
  }

  prompt_player_enable(ENABLE);
  const int32_t currentGain = audio_play_get_vol_gain();
  _unmutedVolume = currentGain <= 0
                         ? 0U
                         : static_cast<uint8_t>(currentGain >= 100
                                                    ? 100
                                                    : currentGain);
  _muted = false;
  _begun = true;
  return true;
}

void ChipIntelliAudioClass::end() {
  // A stop can complete a queued prompt. Clear the user callback first so
  // end() never delivers a late completion into application teardown code.
  onFinished(nullptr);
  stop();
  setMuted(false);
  _begun = false;
}

/**
 * @brief 直接按语音 ID 播放 voice.bin 中的一条语音资源。
 *
 * 本函数不会查询命令词表，也没有播报选项；voiceId 本身直接标识要播放的
 * 语音资源。这是它与 playCommand() 的主要区别。
 *
 * @param voiceId 语音资源 ID，取值范围为 0～65535，必须与当前工程烧录的
 *                voice.bin 中的 ID 一致。
 * @param interruptCurrent true 表示中断当前提示音并立即播放；false 表示不
 *                         中断，当前有提示音时将本次请求加入 SDK 播放队列。
 * @return true 表示 SDK 已接受播放请求；false 表示尚未调用 begin()，或 SDK
 *         立即拒绝了请求。返回 true 不保证资源一定存在或最终播放成功。
 */
bool ChipIntelliAudioClass::playVoice(uint16_t voiceId,
                                     bool interruptCurrent) {
  if (!_begun) {
    return false;
  }
  return prompt_play_by_voice_id(
             voiceId,
             hasFinishedCallback() ? sdkPlaybackFinished : nullptr,
             interruptCurrent) == 0U;
}

bool ChipIntelliAudioClass::playVoice(const String &numberText,
                                      bool interruptCurrent) {
  const char *text = numberText.c_str();
  size_t begin = 0U;
  size_t end = numberText.length();
  while (begin < end && isAsciiWhitespace(text[begin])) {
    ++begin;
  }
  while (end > begin && isAsciiWhitespace(text[end - 1U])) {
    --end;
  }
  if (begin == end) {
    return false;
  }

  bool negative = false;
  if (text[begin] == '+' || text[begin] == '-') {
    negative = text[begin] == '-';
    ++begin;
  }
  if (begin == end) {
    return false;
  }

  uint32_t integerPart = 0U;
  bool decimalPointSeen = false;
  bool digitSeen = false;
  bool nonzeroDigitSeen = false;
  size_t fractionalStart = end;
  size_t fractionalDigits = 0U;

  for (size_t index = begin; index < end; ++index) {
    const char current = text[index];
    if (current >= '0' && current <= '9') {
      const uint8_t digit = static_cast<uint8_t>(current - '0');
      digitSeen = true;
      nonzeroDigitSeen = nonzeroDigitSeen || digit != 0U;
      if (decimalPointSeen) {
        ++fractionalDigits;
        if (fractionalDigits > kMaxVoiceSequenceLength) {
          return false;
        }
      } else {
        if (integerPart > (UINT32_MAX - digit) / 10U) {
          return false;
        }
        integerPart = integerPart * 10U + digit;
      }
      continue;
    }

    if (current == '.' && !decimalPointSeen) {
      decimalPointSeen = true;
      fractionalStart = index + 1U;
      continue;
    }
    return false;
  }

  if (!digitSeen) {
    return false;
  }

  uint16_t sequence[kMaxVoiceSequenceLength];
  VoiceSequenceBuilder builder(sequence, kMaxVoiceSequenceLength);
  if (negative && nonzeroDigitSeen) {
    builder.append(kDefaultNumberVoiceIds.negative);
  }
  appendUnsignedNumber(integerPart, kDefaultNumberVoiceIds, builder);

  if (decimalPointSeen && fractionalDigits != 0U) {
    builder.append(kDefaultNumberVoiceIds.decimalPoint);
    for (size_t index = fractionalStart; index < end; ++index) {
      builder.append(kDefaultNumberVoiceIds.digits[text[index] - '0']);
    }
  }

  const size_t count = builder.size();
  return count != 0U &&
         playVoiceSequence(sequence, count, interruptCurrent);
}

bool ChipIntelliAudioClass::playVoiceSequence(const uint16_t *voiceIds,
                                              size_t count,
                                              bool interruptCurrent) {
  if (!_begun || voiceIds == nullptr || count == 0U ||
      count > kMaxVoiceSequenceLength) {
    return false;
  }
  return ci_arduino_prompt_play_voice_sequence(
             voiceIds, static_cast<uint8_t>(count),
             hasFinishedCallback() ? sdkPlaybackFinished : nullptr,
             interruptCurrent) == 0U;
}

size_t ChipIntelliAudioClass::buildNumberVoiceSequence(
    int32_t value, const NumberVoiceIds &voiceIds, uint16_t *output,
    size_t capacity) const {
  VoiceSequenceBuilder builder(output, capacity);
  if (value < 0) {
    builder.append(voiceIds.negative);
  }
  appendUnsignedNumber(signedMagnitude(value), voiceIds, builder);
  return builder.size();
}

size_t ChipIntelliAudioClass::buildFixedPointVoiceSequence(
    int32_t value, uint8_t fractionalDigits,
    const NumberVoiceIds &voiceIds, uint16_t *output,
    size_t capacity) const {
  if (fractionalDigits > 9U) {
    return 0U;
  }
  if (fractionalDigits == 0U) {
    return buildNumberVoiceSequence(value, voiceIds, output, capacity);
  }

  VoiceSequenceBuilder builder(output, capacity);
  if (value < 0) {
    builder.append(voiceIds.negative);
  }

  const uint32_t magnitude = signedMagnitude(value);
  const uint32_t scale = kPowersOfTen[fractionalDigits];
  appendUnsignedNumber(magnitude / scale, voiceIds, builder);
  builder.append(voiceIds.decimalPoint);

  uint32_t remainder = magnitude % scale;
  for (uint8_t digitsLeft = fractionalDigits; digitsLeft > 0U;
       --digitsLeft) {
    const uint32_t divisor = kPowersOfTen[digitsLeft - 1U];
    builder.append(voiceIds.digits[remainder / divisor]);
    remainder %= divisor;
  }
  return builder.size();
}

bool ChipIntelliAudioClass::playNumber(
    int32_t value, const NumberVoiceIds &voiceIds) {
  uint16_t sequence[kMaxVoiceSequenceLength];
  const size_t count = buildNumberVoiceSequence(
      value, voiceIds, sequence, kMaxVoiceSequenceLength);
  return count != 0U && playVoiceSequence(sequence, count);
}

bool ChipIntelliAudioClass::playFixedPoint(
    int32_t value, uint8_t fractionalDigits,
    const NumberVoiceIds &voiceIds) {
  uint16_t sequence[kMaxVoiceSequenceLength];
  const size_t count = buildFixedPointVoiceSequence(
      value, fractionalDigits, voiceIds, sequence,
      kMaxVoiceSequenceLength);
  return count != 0U && playVoiceSequence(sequence, count);
}

bool ChipIntelliAudioClass::playBeep(unsigned int count) {
  if (!_begun || count == 0U || count > MAX_COMBINATION_COUNT) {
    return false;
  }

  cmd_handle_t beepHandle = cmd_info_find_command_by_string("<beep>");
  const uint16_t beepCommandId = cmd_info_get_command_id(beepHandle);
  if (beepCommandId == INVALID_SHORT_ID) {
    return false;
  }

  prompt_play_info_t prompts[MAX_COMBINATION_COUNT];
  for (unsigned int index = 0; index < count; ++index) {
    prompts[index].cmd_id = beepCommandId;
    prompts[index].select_index = 0;
  }

  return prompt_play_by_multi_cmd_id(
             prompts, static_cast<int>(count),
             hasFinishedCallback() ? sdkPlaybackFinished : nullptr) == 0U;
}

/**
 * @brief unsigned long 命令 ID 兼容重载，检查范围后转交 16 位实现。
 *
 * @param commandId 命令词表中的命令 ID；必须在 0～65535 范围内。
 * @param optionIndex 从 0 开始的播报选项索引；-1 表示由资源配置选择。
 * @param interruptCurrent true 表示中断当前提示音；false 表示排队播放。
 * @return commandId 越界时返回 false，否则返回 16 位实现的结果。
 */
bool ChipIntelliAudioClass::playCommand(unsigned long commandId,
                                       int optionIndex,
                                       bool interruptCurrent) {
  if (commandId > UINT16_MAX) {
    return false;
  }
  return playCommand(static_cast<uint16_t>(commandId), optionIndex,
                     interruptCurrent);
}

/**
 * @brief long 命令 ID 兼容重载，检查正负号和范围后转交 16 位实现。
 *
 * @param commandId 命令词表中的命令 ID；必须在 0～65535 范围内。
 * @param optionIndex 从 0 开始的播报选项索引；-1 表示由资源配置选择。
 * @param interruptCurrent true 表示中断当前提示音；false 表示排队播放。
 * @return commandId 为负数或越界时返回 false，否则返回 16 位实现的结果。
 */
bool ChipIntelliAudioClass::playCommand(long commandId, int optionIndex,
                                       bool interruptCurrent) {
  if (commandId < 0 || commandId > UINT16_MAX) {
    return false;
  }
  return playCommand(static_cast<uint16_t>(commandId), optionIndex,
                     interruptCurrent);
}

/**
 * @brief unsigned int 命令 ID 兼容重载，检查范围后转交 16 位实现。
 *
 * @param commandId 命令词表中的命令 ID；必须在 0～65535 范围内。
 * @param optionIndex 从 0 开始的播报选项索引；-1 表示由资源配置选择。
 * @param interruptCurrent true 表示中断当前提示音；false 表示排队播放。
 * @return commandId 越界时返回 false，否则返回 16 位实现的结果。
 */
bool ChipIntelliAudioClass::playCommand(unsigned int commandId,
                                       int optionIndex,
                                       bool interruptCurrent) {
  if (commandId > UINT16_MAX) {
    return false;
  }
  return playCommand(static_cast<uint16_t>(commandId), optionIndex,
                     interruptCurrent);
}

/**
 * @brief int 命令 ID 兼容重载，检查正负号和范围后转交 16 位实现。
 *
 * @param commandId 命令词表中的命令 ID；必须在 0～65535 范围内。
 * @param optionIndex 从 0 开始的播报选项索引；-1 表示由资源配置选择。
 * @param interruptCurrent true 表示中断当前提示音；false 表示排队播放。
 * @return commandId 为负数或越界时返回 false，否则返回 16 位实现的结果。
 */
bool ChipIntelliAudioClass::playCommand(int commandId, int optionIndex,
                                       bool interruptCurrent) {
  if (commandId < 0 || commandId > UINT16_MAX) {
    return false;
  }
  return playCommand(static_cast<uint16_t>(commandId), optionIndex,
                     interruptCurrent);
}

/**
 * @brief 按 16 位命令 ID 查找命令词表，并播放该命令关联的提示音。
 *
 * 与 playVoice() 不同，本函数先通过 commandId 查找命令配置，再根据
 * optionIndex 选择该命令关联的一组提示音；一组选项可以由多段语音组合。
 *
 * @param commandId 命令词表中的 16 位命令 ID，不是 voice.bin 的语音 ID。
 * @param optionIndex 从 0 开始的播报选项索引。-1 表示由 SDK 按资源配置
 *                    选择：随机类型随机选择，否则使用第 0 项。超出已配置
 *                    选项范围的非负值也会回退到上述 SDK 选择规则。
 * @param interruptCurrent true 表示中断当前提示音并播放本命令提示音；false
 *                         表示不打断，当前有提示音时加入 SDK 播放队列。
 * @return true 表示 SDK 已接受播放请求；false 表示尚未调用 begin()、找不到
 *         commandId，或 SDK 立即拒绝了请求。true 不代表播放已经完成。
 */
bool ChipIntelliAudioClass::playCommand(uint16_t commandId, int optionIndex,
                                       bool interruptCurrent) {
  if (!_begun) {
    return false;
  }
  return prompt_play_by_cmd_id(
             commandId, optionIndex,
             hasFinishedCallback() ? sdkPlaybackFinished : nullptr,
             interruptCurrent) == 0U;
}

bool ChipIntelliAudioClass::playSemantic(uint32_t semanticId, int optionIndex,
                                        bool interruptCurrent) {
  if (!_begun) {
    return false;
  }
  return prompt_play_by_semantic_id(
             semanticId, optionIndex,
             hasFinishedCallback() ? sdkPlaybackFinished : nullptr,
             interruptCurrent) == 0U;
}

/**
 * @brief 按命令文本查找命令词表，并播放该命令关联的提示音。
 *
 * commandText 只用于查找资源包内已经配置的命令，不会把任意文本转换成语音，
 * 因此本函数不是 TTS 接口。
 *
 * @param commandText 以 '\0' 结尾的命令文本，必须与资源包中的命令字符串匹配；
 *                    不能为 nullptr 或空字符串。
 * @param optionIndex 从 0 开始的播报选项索引。-1 表示由 SDK 按资源配置
 *                    选择：随机类型随机选择，否则使用第 0 项。超出已配置
 *                    选项范围的非负值也会回退到上述 SDK 选择规则。
 * @param interruptCurrent true 表示中断当前提示音并播放本命令提示音；false
 *                         表示不打断，当前有提示音时加入 SDK 播放队列。
 * @return true 表示 SDK 已接受播放请求；false 表示尚未调用 begin()、文本
 *         无效、找不到对应命令，或 SDK 立即拒绝了请求。true 不代表播放完成。
 */
bool ChipIntelliAudioClass::playCommand(const char *commandText,
                                       int optionIndex,
                                       bool interruptCurrent) {
  if (!_begun || commandText == nullptr || commandText[0] == '\0') {
    return false;
  }

  // The SDK lookup function does not modify this string, but its V2.7.14 C
  // declaration is missing const.
  return prompt_play_by_cmd_string(
             const_cast<char *>(commandText), optionIndex,
             hasFinishedCallback() ? sdkPlaybackFinished : nullptr,
             interruptCurrent) == 0U;
}

bool ChipIntelliAudioClass::stop() {
  return !_begun || prompt_stop_play() == 0U;
}

bool ChipIntelliAudioClass::isPlaying() const {
  return _begun && prompt_is_playing() != 0U;
}

bool ChipIntelliAudioClass::isReady() const {
  return _begun;
}

void ChipIntelliAudioClass::setVolume(uint8_t percent) {
  if (!_begun) {
    return;
  }
  if (percent > 100U) {
    percent = 100U;
  }
  _unmutedVolume = percent;
  audio_play_set_vol_gain(_muted ? 0 : percent);
}

uint8_t ChipIntelliAudioClass::volume() const {
  if (!_begun) {
    return 0U;
  }
  if (_muted) {
    return _unmutedVolume;
  }
  int32_t gain = audio_play_get_vol_gain();
  if (gain <= 0) {
    return 0U;
  }
  if (gain >= 100) {
    return 100U;
  }
  return static_cast<uint8_t>(gain);
}

void ChipIntelliAudioClass::setMuted(bool muted) {
  if (!_begun) {
    return;
  }
  if (_muted == muted) {
    return;
  }

  if (muted) {
    const int32_t gain = audio_play_get_vol_gain();
    _unmutedVolume = gain <= 0
                           ? 0U
                           : static_cast<uint8_t>(gain >= 100 ? 100 : gain);
    _muted = true;
    audio_play_set_vol_gain(0);
    return;
  }

  _muted = false;
  audio_play_set_vol_gain(_unmutedVolume);
}

void ChipIntelliAudioClass::mute() {
  setMuted(true);
}

void ChipIntelliAudioClass::unmute() {
  setMuted(false);
}

bool ChipIntelliAudioClass::isMuted() const {
  return _muted;
}

void ChipIntelliAudioClass::onFinished(FinishedCallback callback,
                                       void *context) {
  taskENTER_CRITICAL();
  _finishedCallback = callback;
  _finishedContext = callback != nullptr ? context : nullptr;
  taskEXIT_CRITICAL();
}

bool ChipIntelliAudioClass::hasFinishedCallback() const {
  taskENTER_CRITICAL();
  bool hasCallback = _finishedCallback != nullptr;
  taskEXIT_CRITICAL();
  return hasCallback;
}

void ChipIntelliAudioClass::sdkPlaybackFinished(void *commandHandle) {
  (void)commandHandle;
  taskENTER_CRITICAL();
  if (ChipIntelliAudio._pendingFinished != UINT32_MAX) {
    ++ChipIntelliAudio._pendingFinished;
  }
  taskEXIT_CRITICAL();
}

void ChipIntelliAudioClass::dispatchFinishedCallbacks() {
  taskENTER_CRITICAL();
  if (_dispatchingFinished) {
    taskEXIT_CRITICAL();
    return;
  }
  _dispatchingFinished = true;
  taskEXIT_CRITICAL();

  while (true) {
    taskENTER_CRITICAL();
    if (_pendingFinished == 0U) {
      _dispatchingFinished = false;
      taskEXIT_CRITICAL();
      return;
    }

    --_pendingFinished;
    FinishedCallback callback = _finishedCallback;
    void *context = _finishedContext;
    taskEXIT_CRITICAL();

    if (callback != nullptr) {
      callback(context);
    }
  }
}
