#include <ChipIntelliTimer.h>

ChipIntelliTimer hardwareTimer;
volatile uint32_t pendingTicks = 0;

void onHardwareTimer() {
  // Hardware timer callbacks run in interrupt context. Keep them short.
  ++pendingTicks;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  if (!hardwareTimer.begin(500000, onHardwareTimer)) {
    Serial.print("Timer start failed: ");
    Serial.println(ChipIntelliTimer::errorString(hardwareTimer.lastError()));
    return;
  }

  Serial.print("Using TIMER");
  Serial.println(static_cast<int>(hardwareTimer.timerNumber()));
}

void loop() {
  static uint32_t handledTicks = 0;
  if (handledTicks != pendingTicks) {
    handledTicks = pendingTicks;
    Serial.print("hardware tick ");
    Serial.println(handledTicks);
  }
}
