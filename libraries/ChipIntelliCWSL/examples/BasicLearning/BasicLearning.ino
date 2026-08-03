#include <ChipIntelliCWSL.h>

static constexpr uint32_t kLearnedCommandId = 2;

void setup() {
  if (ChipIntelliCWSL.begin()) {
    ChipIntelliCWSL.learnCommand(kLearnedCommandId);
  }
}

void loop() {
  delay(1);
}
