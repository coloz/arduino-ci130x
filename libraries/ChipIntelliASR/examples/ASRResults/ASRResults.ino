#include <ChipIntelliASR.h>

void setup() {
  Serial.begin(921600);
  ChipIntelliASR.begin();
  Serial.println("Waiting for offline ASR results...");
}

void loop() {
  ChipIntelliASRResult result;
  while (ChipIntelliASR.read(result)) {
    Serial.print("command=");
    Serial.print(result.commandId);
    Serial.print(" semantic=");
    Serial.print(result.semanticId);
    Serial.print(" score=");
    Serial.print(result.score);
    Serial.print(" text=");
    Serial.println(result.text);
  }
  delay(1);
}

