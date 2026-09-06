#include <EEPROM.h>

uint32_t boots = 0;
bool savePending = false;

void saveCounter() {
  if (!EEPROM.commit()) {
    Serial.println("EEPROM commit failed; retrying in 1 second");
    return;
  }
  savePending = false;
  Serial.print("Boot count: ");
  Serial.println(boots);
}

void setup() {
  Serial.begin(115200);

  if (!EEPROM.begin(64)) {
    Serial.println("EEPROM/NVDM initialization failed");
    return;
  }

  EEPROM.get(0, boots);
  if (boots == 0xffffffffUL) {
    boots = 0;
  }
  ++boots;
  EEPROM.put(0, boots);
  savePending = true;
  saveCounter();
}

void loop() {
  if (savePending) {
    delay(1000);
    // Retry the same buffered count, without counting another boot.
    saveCounter();
  }
}
