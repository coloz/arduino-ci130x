#include <ChipIntelliIR.h>

static uint16_t learned[ChipIntelliIRClass::MaxRawEntries];
static size_t learnedCount = 0;

static void startListening() {
  if (!ChipIntelliIR.startReceive(5000)) {
    Serial.print("Receive start failed: ");
    Serial.println(ChipIntelliIR.errorString());
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
  }

  if (!ChipIntelliIR.begin()) {
    Serial.print("IR init failed: ");
    Serial.println(ChipIntelliIR.errorString());
    return;
  }

  Serial.println("IR ready. 's'=send NEC, 'r'=replay the last learned frame.");
  startListening();
}

void loop() {
  if (ChipIntelliIR.mode() == ChipIntelliIRClass::Mode::None) {
    delay(1000);
    return;
  }

  if (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    ChipIntelliIR.stopReceive();
    bool sent = false;
    if (command == 's') {
      sent = ChipIntelliIR.sendNEC(0x10, 0x20);
    } else if (command == 'r' && learnedCount != 0) {
      sent = ChipIntelliIR.sendRaw(learned, learnedCount);
    }
    if (command == 'r' && learnedCount == 0) {
      Serial.println("No learned frame is available yet.");
    } else if ((command == 's' || command == 'r') && !sent) {
      Serial.print("IR send failed: ");
      Serial.println(ChipIntelliIR.errorString());
    } else if (sent) {
      Serial.println("IR frame queued.");
    }
    while (ChipIntelliIR.isBusy()) {
      delay(1);
    }
    startListening();
  }

  switch (ChipIntelliIR.receiveStatus()) {
    case ChipIntelliIRClass::ReceiveStatus::Ready: {
      if (ChipIntelliIR.readRaw(learned,
                                ChipIntelliIRClass::MaxRawEntries,
                                learnedCount)) {
        Serial.print("Learned raw entries: ");
        Serial.println(learnedCount);
        for (size_t index = 0; index < learnedCount; ++index) {
          if (index != 0) {
            Serial.print(',');
          }
          Serial.print(learned[index]);
        }
        Serial.println();
      } else {
        Serial.print("Raw read failed: ");
        Serial.println(ChipIntelliIR.errorString());
      }
      startListening();
      break;
    }
    case ChipIntelliIRClass::ReceiveStatus::Timeout:
    case ChipIntelliIRClass::ReceiveStatus::Error:
      startListening();
      break;
    default:
      break;
  }

  delay(1);
}
