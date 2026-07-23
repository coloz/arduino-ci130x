#include <ChipIntelliASR.h>

static bool gAsrReady = false;

void setup() {
  Serial.begin(115200);
  gAsrReady = ChipIntelliASR.begin();
  if (!gAsrReady) {
    Serial.println("ASR initialization failed or timed out.");
    return;
  }
  Serial.println("Waiting for offline ASR results...");
}

void loop() {
  if (!gAsrReady) {
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
  }
  delay(1);
}
