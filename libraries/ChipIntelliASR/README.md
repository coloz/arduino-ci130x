# ChipIntelliASR

`ChipIntelliASR` receives the good-result path of the CI13XX SDK without
replacing its ASR tasks, wake-state machine, or generated command handling.
The Arduino profile disables automatic startup, wake, timeout and command
prompts; sketches select every user-facing response explicitly. The recommended API follows the
[OneButton](https://github.com/mathertel/OneButton) pattern: attach handlers in
`setup()` and call one non-blocking `tick()` from `loop()`.

The Arduino integration starts in direct-command mode: ordinary commands are
accepted without a wake word. Enable wake-word gating at runtime when a sketch
needs it:

```cpp
ChipIntelliASR.setWakeWordEnabled(true);   // require a wake word
ChipIntelliASR.setWakeWordEnabled(false);  // accept commands directly
```

The request is queued to the vendor system task so model changes cannot race
prompt playback or ASR result handling. Call it after `begin()` and check the
boolean result. Enabling selects wake model group 1; disabling selects command
model group 0 and cancels the wake-window timer.

```cpp
constexpr uint16_t kLightOnCommandId = 1;  // from cmd_info

void handleLightOn() {
  // Serial, GPIO and asynchronous ChipIntelliAudio calls are safe here.
  Serial.println("Light on");
}

void setup() {
  Serial.begin(115200);
  if (!ChipIntelliASR.begin()) {  // waits up to 10 seconds by default
    return;
  }
  if (!ChipIntelliASR.attachCommand(kLightOnCommandId, handleLightOn)) {
    Serial.println(ChipIntelliASR.errorString(ChipIntelliASR.lastError()));
  }
}

void loop() {
  ChipIntelliASR.tick();
}
```

## Lifecycle and prompt events

Startup completion, wake-word entry and wake-window timeout use the same
non-blocking event model as commands:

```cpp
void handleStartup() { ChipIntelliAudio.playVoice(1000); }
void handleWakeup()  { ChipIntelliAudio.playVoice(199); }
void handleTimeout() { ChipIntelliAudio.playVoice(0); }

void setup() {
  ChipIntelliASR.attachStartup(handleStartup);
  ChipIntelliASR.attachWakeup(handleWakeup);
  ChipIntelliASR.attachTimeout(handleTimeout);
  if (ChipIntelliASR.begin()) {
    ChipIntelliASR.setWakeWordEnabled(true);
  }
}

void loop() {
  ChipIntelliASR.tick();
}
```

Handlers can be attached before or after `begin()` as long as `tick()` has not
yet consumed the queued event. `detachStartup()`, `detachWakeup()` and
`detachTimeout()` remove them. All four official automatic-prompt switches are
off in the packaged SDK: `PLAY_WELCOME_EN`, `PLAY_ENTER_WAKEUP_EN`,
`PLAY_EXIT_WAKEUP_EN` and `PLAY_OTHER_CMD_EN`. Calling
`ChipIntelliAudio.playVoice()` is therefore always an explicit sketch choice.

`attachCommand()` accepts any of these handler forms:

```cpp
void simpleHandler();
void resultHandler(const ChipIntelliASRResult &result);
void contextHandler(const ChipIntelliASRResult &result, void *context);
void *myContext = nullptr;

ChipIntelliASR.attachCommand(1, simpleHandler);
ChipIntelliASR.attachCommand(2, resultHandler);
ChipIntelliASR.attachCommand(3, contextHandler, myContext);
```

One handler can be attached to each command ID, and registering the same ID
again replaces its handler. Wake words also have command IDs and can be
attached in the same way. `detachCommand()` and `detachAllCommands()` remove
command handlers.

The official CI1302 resources support up to 300 command words and CI1303
resources up to 500. Command and semantic handlers therefore share a
deterministic 512-entry table instead of imposing a small Arduino-side limit.
The table is allocated in static RAM and never fragments the RTOS heap. Use
`handlerCount()` and `handlerCapacity()` to inspect it.

For the smallest complete command-to-action example, including asynchronous
prompt playback from the event handler, open
`File > Examples > ChipIntelliASR > SimpleCommandPlayback`.

## Semantic events

Several recognized phrases can share one 32-bit semantic ID in `cmd_info`.
Registering that ID handles every phrase without repeating the application
logic:

```cpp
constexpr uint32_t kAirConditionerOnSemanticId = 0x01E41943UL;

void handleAirConditionerOn(const ChipIntelliASRResult &result) {
  Serial.print("Matched phrase: ");
  Serial.println(result.text);
}

ChipIntelliASR.attachSemantic(kAirConditionerOnSemanticId,
                              handleAirConditionerOn);
```

`detachSemantic()` and `detachAllSemantics()` remove semantic handlers, while
`detachAll()` removes both kinds. Dispatch order is deliberate: `onResult()`
first observes every result, then an exact command-ID handler runs; the
semantic handler is used only when that command has no exact handler. This
makes semantic handlers convenient defaults and command handlers precise
overrides.

The Core hooks copy SDK results and lifecycle notifications into separate
eight-entry FreeRTOS queues with zero wait time, so vendor tasks never wait for
sketch code. A monotonic sequence preserves their production order. Each
`tick()` immediately returns when both queues are empty and dispatches at most
one item otherwise. All attached handlers and `onResult()` run in the Arduino
`loop()` task rather than an SDK task, so normal Arduino APIs are safe.
Keep handlers short when other loop work needs low latency. `delay()` yields to
the RTOS and does not stop ASR/audio tasks, but it still postpones the next
`loop()` and event dispatch; prefer asynchronous audio calls and `millis()`
state machines for longer application sequences.

`begin(timeoutMs)` starts the shared vendor SDK once and waits until its
initialization task has created the resources required by the default ASR flow
and the audio input path reports that it has started. It returns `false` on
task/resource initialization failure or timeout.
A timeout does not stop an SDK task that is already starting; a later call to
`begin()` can attach after that shared task becomes ready.

Each command string is copied into a 64-byte field. `Result::textTruncated` is
`true` when the SDK text did not fit. The SDK-side copy examines at most 64
bytes, so malformed text cannot create an unbounded scan in the SDK message
task. `pendingResults()` and `pendingEvents()` report the two queue depths;
`droppedResults()` and `droppedEvents()` report their overflow counters.

For compatibility, sketches can manually poll with `available()` and `read()`
instead of registering events. `tick()` and `read()` consume the same queue, so
do not mix them unless consuming results from both paths is intentional.

The SDK's canonical result hook exposes command/semantic IDs, score and text
for wake words and ordinary commands. `frames` is preserved when the normal
command path provides the original message; it is `0` for results such as wake
words where the canonical SDK hook does not expose a frame count.

Results forwarded from a learned CWSL template have no vendor ASR confidence
or frame count, so both `score` and `frames` are reported as `0`. Use the
`distance` field of the corresponding `CWSLRecognized` event to evaluate the
learned-template match.

`end()` only detaches this library's result listener and clears its queue. It
does not shut down the shared SDK because audio and other SDK services may be
using it.

The ASR engine is a hardware singleton, just like Arduino `Serial`. The class
cannot be copied or constructed independently; use the global
`ChipIntelliASR` object or `ChipIntelliASRClass::instance()`.

When `begin()`, `setWakeWordEnabled()`, `attachCommand()`, `attachSemantic()`, or `tick()` returns
`false`, `lastError()` distinguishes disabled ASR, queue allocation and SDK
startup failures, timeout, an invalid callback, a full handler table, a
recursive `tick()` call, a call made before `begin()`, and a rejected control
request. `errorString()` provides a static printable message without allocating
memory.

## Wake and command window

Each result exposes `isWakeWord`, and `isAwake()` reports whether the vendor
recognizer's command session is awake rather than in wake-word-only mode.
Because wake-word gating is disabled by default, `isAwake()` normally remains
`true`; call `setWakeWordEnabled(true)` to use the finite wake/command window.
`keepAwakeFor(timeoutMs)` restarts the official SDK wake timer only when the
recognizer is already awake; it never turns a stale queued result into a new
wake event. A non-AEC profile may still pause recognition temporarily while a
prompt is playing without changing that session state.

Use these APIs and `attachTimeout()` to implement a rolling command window
without maintaining a second sketch-side state machine:

```cpp
void refreshWindow(const ChipIntelliASRResult &) {
  ChipIntelliASR.keepAwakeFor(10000U);
}
```

Register the callback with `onResult(refreshWindow)` and call `tick()` in
`loop()`. Each dispatched wake word or command receives a fresh 10-second
window. On timeout the vendor state machine first returns to its wake-word-only
listening state and then queues the timeout event. This state is voice-session
sleep, not MCU deep sleep. See
`File > Examples > ChipIntelliASR > WakeCommandWindow` for a complete example
where the sketch selects startup, wake, timeout and command responses.

## AEC and voice interruption

The default Arduino algorithm profile enables the vendor acoustic echo
cancellation (AEC) pipeline and keeps the microphone and ASR task running while
a prompt is playing. Both configured wake words and commands can interrupt
playback. This behavior is automatic; a sketch does not need to stop the prompt
from an ASR callback.

Use the profile-query API when a sketch must diagnose its build configuration:

```cpp
Serial.println(ChipIntelliASR.isAECEnabled());
Serial.println(ChipIntelliASR.isBargeInEnabled());

ChipIntelliASRClass::BargeInMode mode = ChipIntelliASR.bargeInMode();
```

`BargeInMode` reports `Disabled`, `WakeWordOnly`, `CommandOnly`, or
`WakeWordAndCommand`. The packaged AEC profiles use `WakeWordAndCommand`.
Selecting either algorithm profile marked “without AEC” pauses the microphone
and ASR task during prompt playback, so its mode is `Disabled`.

AEC requires a real playback-reference signal. The packaged single-microphone
profile captures the microphone on the internal Codec left channel and the
reference on its right channel. Route a line-level signal from before the power
amplifier to the reference input according to the module schematic; never feed
a speaker-level output directly into a Codec input. Firmware selection alone
cannot compensate for a missing, clipped, or incorrectly scaled reference.

See `File > Examples > ChipIntelliASR > BargeIn` for an interactive test.

## Pin ownership after `begin()`

The selected vendor board profile configures its audio power-amplifier control
pin during ASR initialization. Do not use that pin for an LED, GPIO, or analog
input while the SDK is active:

| Variant | SDK-owned pin | Arduino alias |
| --- | --- | --- |
| CI1302 | PC4 | `PC4`, `A0`, digital 20 |
| CI1303 | PC4 | `PC4`, `A0`, digital 20 |
| CI1306 | PD0 | `PD0`, digital 22 |

Other optional SDK settings can claim additional peripheral pins. In the
packaged default profile, UART2 logging is disabled so Arduino retains UART2
until a sketch explicitly opens it.
