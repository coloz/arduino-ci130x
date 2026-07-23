#include <Preferences.h>

Preferences preferences;

void setup() {
  Serial.begin(115200);

  if (!preferences.begin("app")) {
    Serial.println("Preferences initialization failed");
    return;
  }

  uint32_t starts = preferences.getUInt("starts", 0);
  ++starts;
  if (preferences.putUInt("starts", starts) != sizeof(starts)) {
    Serial.println("Could not save the counter");
    return;
  }

  preferences.putString("board", "CI130X");
  Serial.print("Start count: ");
  Serial.println(starts);
  Serial.print("Board: ");
  Serial.println(preferences.getString("board", "unknown"));
}

void loop() {}
