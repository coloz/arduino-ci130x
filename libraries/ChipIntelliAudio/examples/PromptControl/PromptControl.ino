#include <ChipIntelliAudio.h>

// Replace these values with IDs/text from the command package provisioned on
// your board. Playback is only attempted after a command arrives on Serial.
constexpr uint16_t kVoiceId = 1;
constexpr uint16_t kCommandId = 1;
constexpr uint32_t kSemanticId = 1;
const char kCommandText[] = "replace with a configured command";

void printHelp() {
  Serial.println("v: voice ID, c: command ID, s: semantic ID");
  Serial.println("t: command text, x: stop, + / -: volume");
}

void setup() {
  Serial.begin(921600);
  ChipIntelliAudio.begin();
  ChipIntelliAudio.setVolume(70);
  printHelp();
}

void loop() {
  if (!Serial.available()) {
    delay(1);
    return;
  }

  char command = static_cast<char>(Serial.read());
  bool accepted = true;

  switch (command) {
    case 'v':
      accepted = ChipIntelliAudio.playVoice(kVoiceId);
      break;
    case 'c':
      accepted = ChipIntelliAudio.playCommand(kCommandId);
      break;
    case 's':
      accepted = ChipIntelliAudio.playSemantic(kSemanticId);
      break;
    case 't':
      accepted = ChipIntelliAudio.playCommand(kCommandText);
      break;
    case 'x':
      accepted = ChipIntelliAudio.stop();
      break;
    case '+':
      ChipIntelliAudio.setVolume(ChipIntelliAudio.volume() + 10U);
      break;
    case '-': {
      uint8_t current = ChipIntelliAudio.volume();
      ChipIntelliAudio.setVolume(current > 10U ? current - 10U : 0U);
      break;
    }
    case '\r':
    case '\n':
      return;
    default:
      printHelp();
      return;
  }

  Serial.print(accepted ? "accepted" : "rejected");
  Serial.print("; playing=");
  Serial.print(ChipIntelliAudio.isPlaying() ? "yes" : "no");
  Serial.print("; volume=");
  Serial.println(ChipIntelliAudio.volume());
}
