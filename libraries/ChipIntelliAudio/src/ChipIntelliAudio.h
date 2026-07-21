#pragma once

#include <Arduino.h>
#include <stdint.h>

class ChipIntelliAudioClass {
public:
  using FinishedCallback = void (*)(void *context);

  ChipIntelliAudioClass();

  // The vendor SDK creates the audio task during board startup. begin() only
  // enables prompt playback; it does not create or replace that task.
  bool begin();
  void end();

  // Play audio already provisioned in the board's voice.bin partition.
  // Set interruptCurrent to false to append to the SDK prompt queue.
  bool playVoice(uint16_t voiceId, bool interruptCurrent = true);
  bool playCommand(unsigned long commandId, int optionIndex = -1,
                   bool interruptCurrent = true);
  bool playCommand(long commandId, int optionIndex = -1,
                   bool interruptCurrent = true);
  bool playCommand(unsigned int commandId, int optionIndex = -1,
                   bool interruptCurrent = true);
  bool playCommand(int commandId, int optionIndex = -1,
                   bool interruptCurrent = true);
  bool playCommand(uint16_t commandId, int optionIndex = -1,
                   bool interruptCurrent = true);
  bool playSemantic(uint32_t semanticId, int optionIndex = -1,
                    bool interruptCurrent = true);
  bool playCommand(const char *commandText, int optionIndex = -1,
                   bool interruptCurrent = true);

  // stop() is synchronous and may wait for the SDK player to become idle.
  bool stop();
  bool isPlaying() const;

  void setVolume(uint8_t percent);
  uint8_t volume() const;

  // Callback runs in the SDK audio/prompt task. Keep it short and hand longer
  // work back to loop(). Pass nullptr to clear the callback.
  void onFinished(FinishedCallback callback, void *context = nullptr);

private:
  static void sdkPlaybackFinished(void *commandHandle);
  void notifyFinished();
  bool hasFinishedCallback() const;

  FinishedCallback _finishedCallback;
  void *_finishedContext;
};

extern ChipIntelliAudioClass ChipIntelliAudio;
