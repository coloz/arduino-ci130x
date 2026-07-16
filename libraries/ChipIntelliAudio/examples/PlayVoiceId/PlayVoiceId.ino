#include <ChipIntelliAudio.h>

// Replace this with a voice ID present in the voice.bin provisioned on your
// board. Arduino user-code uploads do not replace the voice.bin partition.
constexpr uint16_t kVoiceId = 1;

volatile bool playbackFinished = false;

void onPlaybackFinished(void *context) {
  (void)context;
  playbackFinished = true;
}

void setup() {
  Serial.begin(921600);

  ChipIntelliAudio.begin();
  ChipIntelliAudio.setVolume(70);
  ChipIntelliAudio.onFinished(onPlaybackFinished);

  if (ChipIntelliAudio.playVoice(kVoiceId)) {
    Serial.println("Prompt playback started");
  } else {
    Serial.println("Prompt playback request failed");
  }
}

void loop() {
  if (playbackFinished) {
    playbackFinished = false;
    Serial.println("Prompt playback finished");
  }
  delay(1);
}
