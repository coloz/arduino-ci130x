#include "ChipIntelliAudio.h"

extern "C" {
#include "FreeRTOS.h"
#include "audio_play_api.h"
#include "ci_flash_data_info.h"
#include "prompt_player.h"
#include "system_msg_deal.h"
#include "task.h"

uint32_t ci_arduino_audio_started_message_count(void);
}

namespace {
constexpr uint32_t kInitTimeoutMs = 10000;
constexpr uint32_t kIdleStabilityMs = 500;

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

bool ChipIntelliAudioClass::playCommand(unsigned long commandId,
                                       int optionIndex,
                                       bool interruptCurrent) {
  if (commandId > UINT16_MAX) {
    return false;
  }
  return playCommand(static_cast<uint16_t>(commandId), optionIndex,
                     interruptCurrent);
}

bool ChipIntelliAudioClass::playCommand(long commandId, int optionIndex,
                                       bool interruptCurrent) {
  if (commandId < 0 || commandId > UINT16_MAX) {
    return false;
  }
  return playCommand(static_cast<uint16_t>(commandId), optionIndex,
                     interruptCurrent);
}

bool ChipIntelliAudioClass::playCommand(unsigned int commandId,
                                       int optionIndex,
                                       bool interruptCurrent) {
  if (commandId > UINT16_MAX) {
    return false;
  }
  return playCommand(static_cast<uint16_t>(commandId), optionIndex,
                     interruptCurrent);
}

bool ChipIntelliAudioClass::playCommand(int commandId, int optionIndex,
                                       bool interruptCurrent) {
  if (commandId < 0 || commandId > UINT16_MAX) {
    return false;
  }
  return playCommand(static_cast<uint16_t>(commandId), optionIndex,
                     interruptCurrent);
}

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
