#include "ChipIntelliAudio.h"

extern "C" {
#include "FreeRTOS.h"
#include "audio_play_api.h"
#include "prompt_player.h"
#include "task.h"
}

ChipIntelliAudioClass ChipIntelliAudio;

ChipIntelliAudioClass::ChipIntelliAudioClass()
    : _finishedCallback(nullptr), _finishedContext(nullptr) {}

bool ChipIntelliAudioClass::begin() {
  prompt_player_enable(ENABLE);
  return true;
}

void ChipIntelliAudioClass::end() {
  stop();
  onFinished(nullptr);
}

bool ChipIntelliAudioClass::playVoice(uint16_t voiceId,
                                     bool interruptCurrent) {
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
  return prompt_play_by_cmd_id(
             commandId, optionIndex,
             hasFinishedCallback() ? sdkPlaybackFinished : nullptr,
             interruptCurrent) == 0U;
}

bool ChipIntelliAudioClass::playSemantic(uint32_t semanticId, int optionIndex,
                                        bool interruptCurrent) {
  return prompt_play_by_semantic_id(
             semanticId, optionIndex,
             hasFinishedCallback() ? sdkPlaybackFinished : nullptr,
             interruptCurrent) == 0U;
}

bool ChipIntelliAudioClass::playCommand(const char *commandText,
                                       int optionIndex,
                                       bool interruptCurrent) {
  if (commandText == nullptr || commandText[0] == '\0') {
    return false;
  }

  // The SDK lookup function does not modify this string, but its V2.7.12 C
  // declaration is missing const.
  return prompt_play_by_cmd_string(
             const_cast<char *>(commandText), optionIndex,
             hasFinishedCallback() ? sdkPlaybackFinished : nullptr,
             interruptCurrent) == 0U;
}

bool ChipIntelliAudioClass::stop() {
  return prompt_stop_play() == 0U;
}

bool ChipIntelliAudioClass::isPlaying() const {
  return prompt_is_playing() != 0U;
}

void ChipIntelliAudioClass::setVolume(uint8_t percent) {
  if (percent > 100U) {
    percent = 100U;
  }
  audio_play_set_vol_gain(percent);
}

uint8_t ChipIntelliAudioClass::volume() const {
  int32_t gain = audio_play_get_vol_gain();
  if (gain <= 0) {
    return 0U;
  }
  if (gain >= 100) {
    return 100U;
  }
  return static_cast<uint8_t>(gain);
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
  ChipIntelliAudio.notifyFinished();
}

void ChipIntelliAudioClass::notifyFinished() {
  taskENTER_CRITICAL();
  FinishedCallback callback = _finishedCallback;
  void *context = _finishedContext;
  taskEXIT_CRITICAL();

  if (callback != nullptr) {
    callback(context);
  }
}
