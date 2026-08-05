#include <ChipIntelliASR.h>
#include <ChipIntelliAudio.h>

// IDs from this sketch's generated air-conditioner resource package.
constexpr uint16_t kAirConditionerOnCommandId = 2;
constexpr uint16_t kStartupVoiceId = 15;
constexpr uint16_t kWakeupVoiceId = 1;
constexpr uint16_t kTimeoutVoiceId = 17;
constexpr uint16_t kAirConditionerOnVoiceId = 2;
constexpr uint32_t kCommandWindowMs = 10000U;

bool ready = false;

void handleStartup() {
  Serial.println("ASR started");
  ChipIntelliAudio.playVoice(kStartupVoiceId);
}

void handleWakeup() {
  Serial.println("Wake word recognized");
  ChipIntelliASR.keepAwakeFor(kCommandWindowMs);
  ChipIntelliAudio.playVoice(kWakeupVoiceId);
}

void handleTimeout() {
  Serial.println("Wake window timed out");
  ChipIntelliAudio.playVoice(kTimeoutVoiceId);
}

void handleCommand() {
  Serial.println("Air conditioner on command recognized");
  ChipIntelliASR.keepAwakeFor(kCommandWindowMs);
  ChipIntelliAudio.playVoice(kAirConditionerOnVoiceId);
}

void setup() {
  Serial.begin(115200);

  // Handlers may be attached before begin(). Every callback still runs later
  // from tick(), never from an SDK initialization or recognition task.
  ChipIntelliASR.attachStartup(handleStartup);
  ChipIntelliASR.attachWakeup(handleWakeup);
  ChipIntelliASR.attachTimeout(handleTimeout);
  ChipIntelliASR.attachCommand(kAirConditionerOnCommandId, handleCommand);

  if (!ChipIntelliAudio.begin() || !ChipIntelliASR.begin()) {
    Serial.println("Audio/ASR initialization failed");
    return;
  }
  ready = true;
  Serial.println("Say \"小智小智\", then \"打开空调\" within 10 seconds.");
}

void loop() {
  if (ready) {
    ChipIntelliASR.tick();
  }
}
