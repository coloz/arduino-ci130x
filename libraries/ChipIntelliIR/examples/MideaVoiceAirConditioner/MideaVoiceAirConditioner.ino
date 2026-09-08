#include <ChipIntelliASR.h>
#include <ChipIntelliAudio.h>
#include <ChipIntelliIR.h>
#include <Preferences.h>
extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

// Command/voice IDs match the bundled SimpleCommandPlayback ASR resources.
// Wake word: 小智小智. No online generation or login is needed.
constexpr uint16_t kStartupVoice = 15;
constexpr uint16_t kWakeVoice = 1;
constexpr uint16_t kSuccessVoice = 0;
constexpr uint16_t kErrorVoice = 100; // local sentinel: two beeps
constexpr uint16_t kBusyVoice = 100;
constexpr uint16_t kMatchStartVoice = 0;
constexpr uint16_t kMatchSavedVoice = 0;
constexpr uint16_t kMatchCancelledVoice = 0;
constexpr uint16_t kMatchEndedVoice = 100;
constexpr uint16_t kNotMatchingVoice = 100;
constexpr uint16_t kMatchingVoice = 100;

using Air = ChipIntelliIRClass;
Preferences settings;
bool ready = false;
bool settingsReady = false;
bool sending = false;
bool timerArmed = false;
bool timerOffPending = false;
uint32_t timerStartedAt = 0;
bool searching = false;
bool stoppingSearch = false;
bool saveMatch = false;
uint32_t previousCode = 0;
uint32_t chosenCode = 0;
uint32_t stoppedAt = 0;
uint32_t sentAt = 0;
// Shared with the vendor IR task; snapshot only inside critical sections.
volatile uint32_t candidateCode = 0;
volatile bool candidateValid = false;
volatile bool searchFinished = false;

void speak(uint16_t id) {
  const bool ok = id == kErrorVoice ? ChipIntelliAudio.playBeep(2) : ChipIntelliAudio.playVoice(id);
  if (!ok) Serial.println("Audio queue rejected prompt");
}

void irError() {
  Serial.print("IR error: ");
  Serial.println(ChipIntelliIR.errorString());
  speak(kErrorVoice);
}

void onSearch(Air::AirSearchEvent event, int32_t code, void *) {
  // Never call IR, Flash or audio APIs from this vendor-task callback.
  taskENTER_CRITICAL();
  if (event == Air::AirSearchEvent::CodeSent) {
    candidateCode = static_cast<uint32_t>(code); // opaque 32-bit ID
    candidateValid = true;
  } else {
    searchFinished = true;
  }
  taskEXIT_CRITICAL();
}

void startMatch() {
  if (searching || sending || ChipIntelliIR.isBusy()) {
    speak(kBusyVoice);
    return;
  }
  previousCode = ChipIntelliIR.airCode();
  if (!ChipIntelliIR.selectAirBrand(Air::AirBrand::Midea)) { irError(); return; }
  taskENTER_CRITICAL();
  candidateValid = false;
  searchFinished = false;
  taskEXIT_CRITICAL();
  // Each model is tried 3 times, 5 seconds apart, allowing time to confirm.
  if (!ChipIntelliIR.startAirSearch(Air::AirSearchType::CurrentBrandModels,
                                    onSearch, nullptr, 3, 5000)) {
    irError();
    ChipIntelliIR.selectAirCode(previousCode);
    return;
  }
  searching = true;
  stoppingSearch = false;
  saveMatch = false;
  ChipIntelliASR.keepAwakeFor(120000);
  speak(kMatchStartVoice);
  Serial.println("Matching Midea: when the AC responds, send y=save; x=cancel");
}

void stopMatch(bool save) {
  if (!searching) { speak(kNotMatchingVoice); return; }
  if (stoppingSearch) { speak(kBusyVoice); return; }
  // Freeze the candidate heard by the user before requesting asynchronous stop.
  taskENTER_CRITICAL();
  const bool valid = candidateValid;
  chosenCode = candidateCode;
  taskEXIT_CRITICAL();
  if (save && !valid) { speak(kBusyVoice); return; }
  if (!ChipIntelliIR.stopAirSearch()) { irError(); return; }
  saveMatch = save;
  stoppingSearch = true;
  stoppedAt = millis();
}

void handleCommand(uint16_t id) {
  if (id == 17) { startMatch(); return; }
  if (id == 18 || id == 19) { stopMatch(id == 18); return; }
  if (searching) { speak(kMatchingVoice); return; }
  if (sending || ChipIntelliIR.isBusy()) { speak(kBusyVoice); return; }
  if (id == 13) {
    timerArmed = true;
    timerOffPending = false;
    timerStartedAt = millis();
    Serial.println("One-hour OFF timer armed (local MCU timer)");
    speak(13);
    return;
  }
  if (id == 14) {
    timerArmed = false;
    timerOffPending = false;
    Serial.println("OFF timer cancelled");
    speak(14);
    return;
  }
  bool accepted = false;
  switch (id) {
    case 2: accepted = ChipIntelliIR.power(true); break;
    case 3: accepted = ChipIntelliIR.power(false); break;
    case 4: accepted = ChipIntelliIR.sendAir(Air::AirCommand::ModeCool); break;
    case 5: accepted = ChipIntelliIR.sendAir(Air::AirCommand::ModeHeat); break;
    case 6: accepted = ChipIntelliIR.sendAir(Air::AirCommand::TemperatureUp); break;
    case 7: accepted = ChipIntelliIR.sendAir(Air::AirCommand::TemperatureDown); break;
    case 8: accepted = ChipIntelliIR.setTemperature(26); break;
    case 9: accepted = ChipIntelliIR.sendAir(Air::AirCommand::FanUp); break;
    case 10: accepted = ChipIntelliIR.sendAir(Air::AirCommand::FanDown); break;
    case 11: accepted = ChipIntelliIR.sendAir(Air::AirCommand::FanAuto); break;
    case 12: accepted = ChipIntelliIR.sendAir(Air::AirCommand::SleepMode1); break;
    default: return;
  }
  if (!accepted) { irError(); return; }
  if (id == 3) { timerArmed = false; timerOffPending = false; }
  sending = true;
  sentAt = millis();
  Serial.print("IR queued command="); Serial.println(id);
}

void onResult(const ChipIntelliASRResult &result) {
  Serial.print("ASR id="); Serial.print(result.commandId);
  Serial.print(" text="); Serial.println(result.text);
  ChipIntelliASR.keepAwakeFor(searching ? 120000 : 15000);
  if (!result.isWakeWord) handleCommand(result.commandId);
}

void onWakeup() { speak(kWakeVoice); }

void printStatus() {
  Serial.print("MIDEA_READY code="); Serial.print(ChipIntelliIR.airCode());
  Serial.print(" tx="); Serial.print(Air::DefaultTransmitPin);
  Serial.print(" rx="); Serial.print(Air::DefaultReceivePin);
  Serial.print(" searching="); Serial.print(searching);
  Serial.print(" sending="); Serial.print(sending);
  Serial.print(" timer="); Serial.println(timerArmed || timerOffPending);
  Serial.println("Serial: ?=status, 1=on, 0=off, c=cool, h=heat, 6=26C, s=match, y=save, x=cancel");
}

void setup() {
  Serial.begin(115200);
  Serial.println("MideaVoiceAirConditioner boot");
  ChipIntelliASR.onResult(onResult);
  if (!ChipIntelliASR.attachWakeup(onWakeup) ||
      !ChipIntelliAudio.begin() || !ChipIntelliASR.begin()) {
    Serial.println("Audio/ASR init failed"); return;
  }
  ChipIntelliAudio.setVolume(65);
  if (!ChipIntelliIR.beginAirConditioner() ||
      !ChipIntelliIR.selectAirBrand(Air::AirBrand::Midea)) { irError(); return; }
  settingsReady = settings.begin("midea-ir");
  if (settingsReady && settings.isKey("code")) {
    if (!ChipIntelliIR.selectAirCode(settings.getUInt("code"))) {
      Serial.println("Saved code invalid, using first Midea model");
      if (!ChipIntelliIR.selectAirBrand(Air::AirBrand::Midea)) { irError(); return; }
    }
  }
  if (!settingsReady) Serial.println("Preferences unavailable; matching cannot be saved");
  if (!ChipIntelliASR.setWakeWordEnabled(true)) {
    Serial.println("Wake-word configuration failed"); return;
  }
  ready = true;
  printStatus();
  speak(kStartupVoice);
  // No power-on or IR transmission at boot.
}

void loop() {
  if (!ready) { delay(100); return; }
  // Poll completion before dispatching another command; no blocking IR waits.
  if (sending) {
    const Air::AirSendStatus state = ChipIntelliIR.airSendStatus();
    if (state == Air::AirSendStatus::Idle) {
      sending = false;
      Serial.println("IR transmission complete (AC reception is not acknowledged)");
      speak(kSuccessVoice);
    } else if (state == Air::AirSendStatus::Failed || millis() - sentAt > 30000) {
      sending = false;
      Serial.println("IR asynchronous transmission failed/timed out");
      speak(kErrorVoice);
    }
  }
  if (searching) {
    taskENTER_CRITICAL();
    const bool finished = searchFinished;
    taskEXIT_CRITICAL();
    if (finished && !ChipIntelliIR.isBusy()) {
      const bool wasStopped = stoppingSearch;
      searching = false;
      stoppingSearch = false;
      if (!ChipIntelliIR.selectAirCode(saveMatch ? chosenCode : previousCode)) {
        irError();
      } else if (saveMatch) {
        if (settingsReady && settings.putUInt("code", chosenCode) == sizeof(uint32_t)) {
          Serial.print("Saved Midea code="); Serial.println(chosenCode);
          speak(kMatchSavedVoice);
        } else {
          Serial.println("Code selected for this boot, but Flash save failed");
          speak(kErrorVoice);
        }
      } else {
        speak(wasStopped ? kMatchCancelledVoice : kMatchEndedVoice);
      }
      ChipIntelliASR.keepAwakeFor(15000);
    } else if (stoppingSearch && millis() - stoppedAt > 30000) {
      // Keep controls locked if the vendor task never acknowledges stop.
      stoppedAt = millis();
      Serial.println("Search stop timeout; reset board before retrying");
      speak(kErrorVoice);
    }
  }
  if (timerArmed && millis() - timerStartedAt >= 3600000UL) {
    timerArmed = false;
    timerOffPending = true;
  }
  if (timerOffPending && !searching && !sending && !ChipIntelliIR.isBusy()) {
    timerOffPending = false; // a failed attempt is reported, never retried forever
    Serial.println("OFF timer expired");
    handleCommand(3);
  }
  ChipIntelliASR.tick();
  if (Serial.available()) {
    switch (Serial.read()) {
      case '?': printStatus(); break;
      case '1': handleCommand(2); break;
      case '0': handleCommand(3); break;
      case 'c': handleCommand(4); break;
      case 'h': handleCommand(5); break;
      case '6': handleCommand(8); break;
      case 's': handleCommand(17); break;
      case 'y': handleCommand(18); break;
      case 'x': handleCommand(19); break;
    }
  }
}
