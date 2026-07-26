#include <ChipIntelliCWSL.h>

static constexpr uint32_t kWakeWordCommandId = 1;
static constexpr uint32_t kLearnedCommandId = 2;

static const char *eventName(ChipIntelliCWSLEventType type) {
  switch (type) {
    case CWSLLearningStarted: return "learning-started";
    case CWSLRecordingStarted: return "recording-started";
    case CWSLAttemptResult: return "attempt-result";
    case CWSLLearningSucceeded: return "learning-succeeded";
    case CWSLLearningFailed: return "learning-failed";
    case CWSLLearningCancelled: return "learning-cancelled";
    case CWSLDeleteSucceeded: return "delete-succeeded";
    case CWSLRecognized: return "recognized";
    case CWSLDeleteFailed: return "delete-failed";
    default: return "unknown";
  }
}

static void printHelp() {
  Serial.println("l: learn command ID 2");
  Serial.println("w: learn wake-word command ID 1");
  Serial.println("c: cancel learning");
  Serial.println("d: delete learned command ID 2");
  Serial.println("x: delete all learned templates");
  Serial.println("s: print template status");
}

static void printStatus() {
  Serial.print("templates=");
  Serial.print(ChipIntelliCWSL.templateCount());
  Serial.print(" remaining=");
  Serial.print(ChipIntelliCWSL.remainingTemplates());
  Serial.print(" max=");
  Serial.println(ChipIntelliCWSL.maxTemplates());
}

void setup() {
  Serial.begin(115200);
  if (!ChipIntelliCWSL.profileEnabled()) {
    Serial.println("Select Tools > Algorithm configuration > CWSL.");
    return;
  }
  if (!ChipIntelliCWSL.begin()) {
    Serial.println("CWSL initialization failed or timed out.");
    return;
  }
  printHelp();
  printStatus();
}

void loop() {
  ChipIntelliCWSLEvent event;
  while (ChipIntelliCWSL.read(event)) {
    Serial.print(eventName(event.type));
    Serial.print(" command=");
    Serial.print(event.commandId);
    Serial.print(" attempt=");
    Serial.print(event.attempt);
    Serial.print(" result=");
    Serial.print(static_cast<unsigned int>(event.result));
    Serial.print(" distance=");
    Serial.println(event.distance);
  }

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'l':
        Serial.println(ChipIntelliCWSL.learnCommand(kLearnedCommandId)
                           ? "Say the new command now."
                           : "Could not start command learning.");
        break;
      case 'w':
        Serial.println(ChipIntelliCWSL.learnWakeWord(kWakeWordCommandId)
                           ? "Say the new wake word now."
                           : "Could not start wake-word learning.");
        break;
      case 'c':
        Serial.println(ChipIntelliCWSL.cancelLearning()
                           ? "Learning cancelled."
                           : "No active learning operation.");
        break;
      case 'd':
        Serial.println(ChipIntelliCWSL.eraseCommand(kLearnedCommandId)
                           ? "Delete requested."
                           : "Could not delete the command template.");
        break;
      case 'x':
        Serial.println(ChipIntelliCWSL.eraseAll()
                           ? "Delete-all requested."
                           : "Could not delete templates.");
        break;
      case 's':
        printStatus();
        break;
      default:
        printHelp();
        break;
    }
  }
  delay(1);
}
