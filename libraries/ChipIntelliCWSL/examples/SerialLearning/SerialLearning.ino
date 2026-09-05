#include <ChipIntelliAudio.h>
#include <ChipIntelliCWSL.h>

static constexpr uint32_t kWakeWordCommandId = 1;
static constexpr uint32_t kLearnedCommandId = 2;
// Official CI130X SDK defaults: CWSL_REGISTRATION_WAKE is command 200.
// Non-continuous command learning first plays CWSL_REGISTRATION_CMD (201),
// then reg_cmd_list maps learnable command 2 to spoken prompt command 1001.
static constexpr uint16_t kWakeWordLearningPromptId = 200;
static constexpr uint16_t kCommandLearningIntroPromptId = 201;
static constexpr uint16_t kCommandLearningPromptId = 1001;
static constexpr uint8_t kPromptVolume = 70;

enum PendingLearning : uint8_t {
  PendingNone,
  PendingCommand,
  PendingWakeWord,
};

enum LearningPromptStage : uint8_t {
  PromptIdle,
  PromptCommandIntro,
  PromptFinal,
};

static PendingLearning pendingLearning = PendingNone;
static LearningPromptStage promptStage = PromptIdle;
static volatile bool promptFinished = false;

static void onPromptFinished(void *context) {
  (void)context;
  promptFinished = true;
}

static void printError(const char *message) {
  Serial.print(message);
  Serial.print(": ");
  Serial.println(ChipIntelliCWSL.errorString());
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
  Serial.print("state=");
  Serial.print(ChipIntelliCWSL.stateName(ChipIntelliCWSL.state()));
  Serial.print(" templates=");
  Serial.print(ChipIntelliCWSL.templateCount());
  Serial.print(" remaining=");
  Serial.print(ChipIntelliCWSL.remainingTemplates());
  Serial.print(" max=");
  Serial.print(ChipIntelliCWSL.maxTemplates());
  Serial.print(" dropped-read=");
  Serial.print(ChipIntelliCWSL.droppedReadEvents());
  Serial.print(" dropped-callback=");
  Serial.println(ChipIntelliCWSL.droppedCallbackEvents());
}

static void requestPromptedLearning(PendingLearning request) {
  if (pendingLearning != PendingNone) {
    Serial.println("A learning prompt is already active.");
    return;
  }

  promptFinished = false;
  pendingLearning = request;
  promptStage = request == PendingCommand ? PromptCommandIntro : PromptFinal;
  const uint16_t promptId = request == PendingCommand
                                ? kCommandLearningIntroPromptId
                                : kWakeWordLearningPromptId;
  if (!ChipIntelliAudio.playCommand(promptId)) {
    pendingLearning = PendingNone;
    promptStage = PromptIdle;
    Serial.println("Could not play the learning prompt.");
    return;
  }
  Serial.println("Listen for the official learning prompts, then speak.");
}

static void servicePromptedLearning() {
  if (!promptFinished) {
    return;
  }

  promptFinished = false;

  if (pendingLearning == PendingCommand &&
      promptStage == PromptCommandIntro) {
    promptStage = PromptFinal;
    if (!ChipIntelliAudio.playCommand(kCommandLearningPromptId)) {
      pendingLearning = PendingNone;
      promptStage = PromptIdle;
      Serial.println("Could not play the command-specific learning prompt.");
    } else {
      Serial.println("Playing the command-specific learning prompt.");
    }
    return;
  }

  const PendingLearning request = pendingLearning;
  pendingLearning = PendingNone;
  promptStage = PromptIdle;

  if (request == PendingCommand) {
    if (ChipIntelliCWSL.learnCommand(kLearnedCommandId)) {
      Serial.println("Prompts finished. Say the new command now.");
    } else {
      printError("Could not start command learning");
    }
  } else if (request == PendingWakeWord) {
    if (ChipIntelliCWSL.learnWakeWord(kWakeWordCommandId)) {
      Serial.println("Prompt finished. Say the new wake word now.");
    } else {
      printError("Could not start wake-word learning");
    }
  }
}

void setup() {
  Serial.begin(115200);
  if (!ChipIntelliCWSL.profileEnabled()) {
    Serial.println("Select Tools > Algorithm configuration > CWSL.");
    return;
  }
  if (!ChipIntelliCWSL.begin()) {
    printError("CWSL initialization failed");
    return;
  }
  if (!ChipIntelliAudio.begin()) {
    Serial.println("Learning-prompt audio initialization failed.");
    return;
  }
  ChipIntelliAudio.setVolume(kPromptVolume);
  ChipIntelliAudio.onFinished(onPromptFinished);
  printHelp();
  printStatus();
}

void loop() {
  servicePromptedLearning();

  ChipIntelliCWSLEvent event;
  while (ChipIntelliCWSL.read(event)) {
    Serial.print(ChipIntelliCWSL.eventName(event.type));
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
        requestPromptedLearning(PendingCommand);
        break;
      case 'w':
        requestPromptedLearning(PendingWakeWord);
        break;
      case 'c':
        if (pendingLearning != PendingNone) {
          pendingLearning = PendingNone;
          promptStage = PromptIdle;
          promptFinished = false;
          ChipIntelliAudio.stop();
          Serial.println("Pending learning cancelled.");
        } else {
          if (ChipIntelliCWSL.cancelLearning()) {
            Serial.println("Learning cancellation requested.");
          } else {
            printError("Could not cancel learning");
          }
        }
        break;
      case 'd':
        if (ChipIntelliCWSL.eraseCommand(kLearnedCommandId)) {
          Serial.println("Delete requested.");
        } else {
          printError("Could not delete the command template");
        }
        break;
      case 'x':
        if (ChipIntelliCWSL.eraseAll()) {
          Serial.println("Delete-all requested.");
        } else {
          printError("Could not delete templates");
        }
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
