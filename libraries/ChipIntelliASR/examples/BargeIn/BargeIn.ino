#include <ChipIntelliASR.h>
#include <ChipIntelliAudio.h>

// Voice ID 16 is a long instruction recorded specifically for this test.
static constexpr uint16_t kTestVoiceId = 16;

static bool gReady = false;

static void printProfile() {
  Serial.print("AEC: ");
  Serial.println(ChipIntelliASR.isAECEnabled() ? "enabled" : "disabled");
  Serial.print("Voice interruption: ");
  Serial.println(ChipIntelliASR.isBargeInEnabled() ? "enabled" : "disabled");
}

static void startTestPrompt() {
  if (ChipIntelliAudio.playVoice(kTestVoiceId)) {
    Serial.println("Prompt started. Speak a configured wake word or command now.");
  } else {
    Serial.println("Prompt request failed. Check voice.bin and kTestVoiceId.");
  }
}

void setup() {
  Serial.begin(115200);

  if (!ChipIntelliAudio.begin() || !ChipIntelliASR.begin()) {
    Serial.println("Audio/ASR initialization failed or timed out.");
    return;
  }

  printProfile();
  if (!ChipIntelliASR.isBargeInEnabled()) {
    Serial.println("Select an AEC algorithm profile in Tools > Algorithm.");
    return;
  }

  gReady = true;
  Serial.println("Send 'p' to replay the test prompt.");
  startTestPrompt();
}

void loop() {
  if (!gReady) {
    delay(1000);
    return;
  }

  if (Serial.available() && Serial.read() == 'p') {
    startTestPrompt();
  }

  ChipIntelliASRResult result;
  while (ChipIntelliASR.read(result)) {
    Serial.print("Barge-in result: command=");
    Serial.print(result.commandId);
    Serial.print(" semantic=");
    Serial.print(result.semanticId);
    Serial.print(" score=");
    Serial.print(result.score);
    Serial.print(" text=");
    Serial.println(result.text);
    // Respond with the prompt associated with the recognized command. The
    // default interruptCurrent=true makes the barge-in audible immediately.
    if (!ChipIntelliAudio.playCommand(result.commandId)) {
      Serial.println("Matching response prompt was rejected.");
    }
    // This example intentionally keeps the compatible read() API. For normal
    // application code, attachCommand()/onResult() plus tick() is preferred.
  }

  delay(1);
}
