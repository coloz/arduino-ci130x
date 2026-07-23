#include <ChipIntelliIR.h>

static void printResult(bool ok) {
  if (ok) {
    Serial.println("Command accepted.");
  } else {
    Serial.print("IR command failed: ");
    Serial.println(ChipIntelliIR.errorString());
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
  }

  if (!ChipIntelliIR.beginAirConditioner()) {
    Serial.print("Air-conditioner IR init failed: ");
    Serial.println(ChipIntelliIR.errorString());
    return;
  }
  if (!ChipIntelliIR.selectAirBrand(
          ChipIntelliIRClass::AirBrand::Gree)) {
    Serial.print("Brand selection failed: ");
    Serial.println(ChipIntelliIR.errorString());
    return;
  }

  Serial.print("Gree first code ID: ");
  Serial.println(ChipIntelliIR.airCode());
  Serial.println("Commands: 1=on, 0=off, 6=26 C, c=cool, h=heat, a=auto.");
}

void loop() {
  if (!Serial.available()) {
    delay(1);
    return;
  }

  switch (Serial.read()) {
    case '1':
      printResult(ChipIntelliIR.power(true));
      break;
    case '0':
      printResult(ChipIntelliIR.power(false));
      break;
    case '6':
      printResult(ChipIntelliIR.setTemperature(26));
      break;
    case 'c':
      printResult(ChipIntelliIR.sendAir(
          ChipIntelliIRClass::AirCommand::ModeCool));
      break;
    case 'h':
      printResult(ChipIntelliIR.sendAir(
          ChipIntelliIRClass::AirCommand::ModeHeat));
      break;
    case 'a':
      printResult(ChipIntelliIR.sendAir(
          ChipIntelliIRClass::AirCommand::ModeAuto));
      break;
    default:
      break;
  }
}
