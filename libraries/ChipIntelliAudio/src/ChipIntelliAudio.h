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
