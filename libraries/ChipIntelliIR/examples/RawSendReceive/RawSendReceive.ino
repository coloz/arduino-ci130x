#include <ChipIntelliIR.h>

// uint32_t preserves NEC repeat gaps and other intervals above 65,535 us.
static uint32_t learned[ChipIntelliIRClass::MaxRawEntries];
static size_t learnedCount = 0;

static const char *frameTypeName(ChipIntelliIRClass::NECFrameType type) {
  switch (type) {
    case ChipIntelliIRClass::NECFrameType::Standard:
      return "standard";
    case ChipIntelliIRClass::NECFrameType::Extended:
      return "extended";
    case ChipIntelliIRClass::NECFrameType::Repeat:
      return "repeat";
    default:
      return "unknown";
  }
}

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

        ChipIntelliIRClass::NECDecodeResult decoded;
        if (ChipIntelliIRClass::decodeNEC(learned, learnedCount, decoded)) {
          Serial.print("NEC ");
          Serial.print(frameTypeName(decoded.type));
          if (decoded.type != ChipIntelliIRClass::NECFrameType::Repeat) {
            Serial.print(" address=0x");
            Serial.print(decoded.address, HEX);
            Serial.print(" command=0x");
            Serial.print(decoded.command, HEX);
          }
          Serial.print(" repeats=");
          Serial.println(decoded.repeatCount);
        }
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
