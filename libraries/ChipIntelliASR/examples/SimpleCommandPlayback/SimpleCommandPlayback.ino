#include <ChipIntelliASR.h>
#include <ChipIntelliAudio.h>

// IDs from this sketch's generated air-conditioner resource package.
constexpr uint16_t kAirConditionerOnCommandId = 2;
constexpr uint16_t kStartupVoiceId = 15;
constexpr uint16_t kWakeupVoiceId = 1;
constexpr uint16_t kTimeoutVoiceId = 17;
constexpr uint16_t kAirConditionerOnVoiceId = 2;

bool ready = false;

void handleStartup() {
  ChipIntelliAudio.playVoice(kStartupVoiceId);
}

void handleWakeup() {
  ChipIntelliAudio.playVoice(kWakeupVoiceId);
}

void handleTimeout() {
  ChipIntelliAudio.playVoice(kTimeoutVoiceId);
}

void handleAirConditionerOn() {
  Serial.println("Air conditioner on command recognized");
  ChipIntelliAudio.playVoice(kAirConditionerOnVoiceId);
}

void setup() {
  Serial.begin(115200);

  const bool handlersReady =
      ChipIntelliASR.attachStartup(handleStartup) &&
      ChipIntelliASR.attachWakeup(handleWakeup) &&
      ChipIntelliASR.attachTimeout(handleTimeout) &&
      ChipIntelliASR.attachCommand(kAirConditionerOnCommandId,
                                   handleAirConditionerOn);
  if (!handlersReady) {
    Serial.println("ASR handler registration failed");
    return;
  }

  if (!ChipIntelliAudio.begin() || !ChipIntelliASR.begin()) {
    Serial.println("Audio/ASR initialization failed");
    return;
  }

  ready = true;
  Serial.println("Say \"小智小智\", then \"打开空调\".");
}

void loop() {
  if (ready) {
    ChipIntelliASR.tick();
  } else {
    delay(1000);
  }
}
