#include <ChipIntelliASR.h>

// These values match the command package in this sketch's recursos directory.
static constexpr uint16_t kAirConditionerOnCommandId = 2;
static constexpr uint32_t kAirConditionerOffSemanticId = 0x01E41983UL;

static bool gAsrReady = false;

// Event handlers live outside loop(), just like OneButton handlers.
static void handleAirConditionerOn() {
  Serial.println("Action: turn the air conditioner on");
}

static void handleAirConditionerOff(const ChipIntelliASRResult &result) {
  Serial.print("Action: turn the air conditioner off, score=");
  Serial.println(result.score);
}

// onResult() is optional. It observes every result before the matching
// command-specific handler runs.
static void logResult(const ChipIntelliASRResult &result) {
  Serial.print("command=");
  Serial.print(result.commandId);
  Serial.print(" semantic=");
  Serial.print(result.semanticId);
  Serial.print(" score=");
  Serial.print(result.score);
  Serial.print(" text=");
  Serial.print(result.text);
  if (result.textTruncated) {
    Serial.print(" [truncated]");
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  gAsrReady = ChipIntelliASR.begin();
  if (!gAsrReady) {
    Serial.print("ASR initialization failed: ");
    Serial.println(ChipIntelliASR.errorString(ChipIntelliASR.lastError()));
    return;
  }

  ChipIntelliASR.onResult(logResult);
  const bool handlersReady =
      ChipIntelliASR.attachCommand(kAirConditionerOnCommandId,
                                   handleAirConditionerOn) &&
      ChipIntelliASR.attachSemantic(kAirConditionerOffSemanticId,
                                    handleAirConditionerOff);
  if (!handlersReady) {
    Serial.print("ASR handler registration failed: ");
    Serial.println(ChipIntelliASR.errorString(ChipIntelliASR.lastError()));
    gAsrReady = false;
    return;
  }

  Serial.print("Registered handlers: ");
  Serial.print(ChipIntelliASR.handlerCount());
  Serial.print('/');
  Serial.println(ChipIntelliASR.handlerCapacity());
  Serial.println("Say \"打开空调\" or \"关闭空调\" directly.");
}

void loop() {
  if (!gAsrReady) {
    delay(1000);
    return;
  }

  // Non-blocking: dispatches at most one queued recognition result per call.
  ChipIntelliASR.tick();
}
