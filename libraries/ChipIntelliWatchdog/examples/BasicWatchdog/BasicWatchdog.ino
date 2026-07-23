#include <ChipIntelliWatchdog.h>

constexpr uint32_t kWatchdogStageMs = 3000;
constexpr uint32_t kFeedIntervalMs = 1000;

void setup() {
  Serial.begin(115200);
  delay(100);

  if (!ChipIntelliWatchdog.begin(kWatchdogStageMs)) {
    Serial.print("Watchdog start failed: ");
    Serial.println(ChipIntelliWatchdog.errorString());
    return;
  }

  Serial.println("Watchdog started");
  Serial.println("Stop calling feed() to test a watchdog reset");
}
void loop() {
  static uint32_t lastFeedMs = 0;
  const uint32_t now = millis();

  if (ChipIntelliWatchdog.isRunning() &&
      now - lastFeedMs >= kFeedIntervalMs) {
    lastFeedMs = now;
    ChipIntelliWatchdog.feed();
    Serial.println("watchdog fed");
  }
}
