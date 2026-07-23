#include <EEPROM.h>

void setup() {
  Serial.begin(115200);

  if (!EEPROM.begin(64)) {
    Serial.println("EEPROM/NVDM initialization failed");
    return;
  }

  uint32_t boots = 0;
  EEPROM.get(0, boots);
  if (boots == 0xffffffffUL) {
    boots = 0;
  }
  ++boots;
  EEPROM.put(0, boots);

  if (EEPROM.commit()) {
    Serial.print("Boot count: ");
    Serial.println(boots);
  } else {
    Serial.println("EEPROM commit failed");
  }
}

void loop() {}
