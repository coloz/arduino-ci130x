#define CHIPINTELLI_LANGUAGE CHIPINTELLI_LANGUAGE_ZH
#include <ChipIntelliAudio.h>

namespace {
int16_t integerValue = 300;
double piValue = 3.1415926;
float negativeValue = -123.5f;
volatile uint8_t finishedCount = 0U;

void onPlaybackFinished(void *) {
  ++finishedCount;
}

bool queueNumbers() {
  // Floating-point String conversion defaults to two fractional digits.
  // Pass the required precision explicitly when more digits must be spoken.
  return ChipIntelliAudio.playVoice(String(integerValue), false) &&
         ChipIntelliAudio.playVoice(String(piValue, 7), false) &&
         ChipIntelliAudio.playVoice(String(negativeValue, 1), false);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  if (!ChipIntelliAudio.begin()) {
    Serial.println("Audio initialization failed");
    return;
  }

  ChipIntelliAudio.setVolume(90);
  ChipIntelliAudio.onFinished(onPlaybackFinished);
  if (!queueNumbers()) {
    Serial.println("Playback queue is full");
  }
}

void loop() {
  static uint8_t reportedCount = 0U;
  if (reportedCount != finishedCount) {
    reportedCount = finishedCount;
    Serial.print("Completed number prompts: ");
    Serial.println(reportedCount);
  }

  // Playback is owned by ChipIntelliAudio's FreeRTOS task, so loop() remains
  // available for the application while all three values are spoken in order.
  delay(1);
}
