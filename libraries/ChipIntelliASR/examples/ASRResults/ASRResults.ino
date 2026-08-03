#include <ChipIntelliASR.h>
#include <ChipIntelliAudio.h>

static bool gAsrReady = false;
static bool gAudioReady = false;

void setup() {
  Serial.begin(115200);

  gAudioReady = ChipIntelliAudio.begin();
  if (!gAudioReady) {
    Serial.println("Audio initialization failed or timed out.");
    return;
  }
  ChipIntelliAudio.setVolume(70);

  gAsrReady = ChipIntelliASR.begin();
  if (!gAsrReady) {
    Serial.println("ASR initialization failed or timed out.");
    return;
  }

  Serial.println("Waiting for offline ASR results and playing matching prompts...");
}

void loop() {
  if (!gAsrReady || !gAudioReady) {
    delay(1000);
    return;
  }

  ChipIntelliASRResult result;
  while (ChipIntelliASR.read(result)) {
    Serial.print("command=");
    Serial.print(result.commandId);
    Serial.print(" semantic=");
    Serial.print(result.semanticId);
    Serial.print(" score=");
    Serial.print(result.score);
    Serial.print(" text=");
    Serial.print(result.text);
    if (result.textTruncated) {
      Serial.print(" [truncated]");
    }
    Serial.println();

    // Use the command metadata bundled with the sketch resources to select
    // the matching response from voice.bin. An option index of -1 lets the
    // SDK choose among the response variants configured for this command.
    // Some SDK configurations start a prompt before publishing the result,
    // so leave that prompt running instead of interrupting and replaying it.
    if (ChipIntelliAudio.isPlaying()) {
      Serial.println("Matching prompt is already playing.");
    } else if (ChipIntelliAudio.playCommand(result.commandId)) {
      Serial.println("Matching prompt request accepted.");
    } else {
      Serial.println("Matching prompt request failed.");
    }
  }
  delay(1);
}
