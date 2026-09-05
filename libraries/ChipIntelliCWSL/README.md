# ChipIntelliCWSL

`ChipIntelliCWSL` exposes the CI130X command-word self-learning engine to an
Arduino sketch. Select a **Tools > Algorithm Profile > CWSL Command-Word Learning**
profile before compiling.
`begin()` returns `false` under the standard offline-ASR profile. Use
`errorString()` whenever a Boolean operation fails; it distinguishes profile
and SDK startup failures from runtime request rejection.

Learning is asynchronous. `learnCommand()` or `learnWakeWord()` starts audio
capture and returns immediately. Poll events with `read()` or install an
`onEvent()` callback. The two delivery paths are independent, so applications
normally choose one. Vendor SDK tasks only copy events into fixed, zero-wait
queues; callbacks execute later in the Arduino event dispatcher and therefore
do not block the capture/recognition task. Keep callbacks bounded because they
share the dispatcher.

Each queue holds 15 complete events. New events are discarded when a queue is
full; draining the polling queue in every `loop()` keeps terminal events visible.
`droppedReadEvents()` and `droppedCallbackEvents()` diagnose each path;
`droppedEvents()` is their combined compatibility counter. A callback-only
application may let the unused read queue fill, so it should inspect
`droppedCallbackEvents()` rather than the combined count.
Control calls made synchronously from `onEvent()` can return `false` while the
current event owns the operation transition. Queue those requests and execute
them from `loop()` instead.
`CWSLRecordingStarted` means that the bridge successfully queued the vendor
record-start request; this vendor binary does not expose a physical-capture
start callback.
All CWSL APIs use task-only SDK services and must be called from `setup()`,
`loop()`, or another RTOS task—not from an ISR or hardware-timer callback.

`BasicLearning` is the size-constrained starting point for CI1302 CWSL builds
on every host OS. The richer `SerialLearning` example, which also includes
`ChipIntelliAudio`, targets CI1303 and CI1306; it exceeds CI1302's merged
user-code/SRAM limit even with the Windows LTO toolchain.

```cpp
#include <ChipIntelliCWSL.h>

void setup() {
  if (!ChipIntelliCWSL.begin()) {
    // Serial.println(ChipIntelliCWSL.errorString());
    return;
  }
  if (!ChipIntelliCWSL.learnCommand(2)) {
    // The command may be missing, have the wrong type, already be learned,
    // or the engine may be busy. errorString() gives the actionable category.
  }
}

void loop() {
  ChipIntelliCWSLEvent event;
  while (ChipIntelliCWSL.read(event)) {
    // Handle learning, deletion, and learned-word recognition events.
  }
}
```

`ChipIntelliCWSL` is the only supported instance because the chip contains one
CWSL engine and the Arduino Core exposes one callback slot. The class is
intentionally non-copyable; use the global object or
`ChipIntelliCWSLClass::instance()`.

Useful diagnostics and convenience helpers:

```cpp
Serial.println(ChipIntelliCWSL.errorString());
Serial.println(ChipIntelliCWSL.stateName(ChipIntelliCWSL.state()));
Serial.println(ChipIntelliCWSL.eventName(event.type));
Serial.println(ChipIntelliCWSL.resultName(event.result));
Serial.println(ChipIntelliCWSL.pendingEvents());
```

`state()` returns `CWSLUnavailable` and template count methods return `-1`
until this library instance has completed `begin()`. `clearEvents()` discards
only the polling backlog; it does not cancel learning, deletion, or callbacks.

`attempt` and `result` are meaningful for learning attempt and terminal
learning events. `distance` is meaningful for `CWSLRecognized`. Delete and
recognition events otherwise leave non-applicable fields at neutral values;
the recognized template's `groupId` is the special value `UINT16_MAX` because
the vendor recognition callback does not report it.

The command ID passed to a learning call must already exist in the active
`cmd_info` resource. `learnWakeWord()` accepts only a command marked as a wake
word, and `learnCommand()` accepts only a non-wake command. CWSL learns a new
spoken phrase that maps to that command; it does not create new command
metadata or text. Command IDs must be no greater than 65535 because the vendor
recognition callback reports a 16-bit command ID; group IDs must be no greater
than 255 because the persisted template stores an 8-bit group. Missing IDs,
type mismatches, values outside these ranges, and the packaged voice-control
IDs 199 through 208 return `false`. The vendor ABI exposes one success/failure
result rather than a detailed rejection code, so runtime failures use
`Error::RequestRejected`; its `errorString()` lists the relevant checks.

Templates are stored by the vendor NVDATA manager and survive reset. The
packaged profile reserves 16 templates; the underlying engine supports up to
32. The vendor flow permits two alternative learned wake phrases to map to the
same wake-command/group pair. A normal command/group pair remains unique and
must be erased before it is learned again. CWSL is incompatible with
`MULT_INTENT > 1` in this SDK.
The vendor recognition callback does not report the stored group, so
`CWSLRecognized` events use `UINT16_MAX` for `groupId`.

The vendor interface has no stop acknowledgement or session ID on record-end
callbacks. `cancelLearning()` is therefore accepted only before
`CWSLLearningStarted`; after learning starts it returns `false` and the current
attempt continues. If system sleep or reset forcibly interrupts an already
queued recording, recognition and deletion remain available, but further
learning calls fail closed until the MCU is restarted. This prevents an old
record-end callback from being applied to a newer learning request.
The same quarantine is applied when the official ASR path detects a default
command during recording and has to generate an early record-end message: the
vendor NN producer may still generate a second, untagged end. The current
learning session is terminated, and learning remains disabled until restart.
For the same reason, an official voice-guided flow that exits or resets while
its record-end callback is still unwinding also latches this quarantine. This
is intentionally conservative; the vendor ABI provides no final drain
acknowledgement with which to re-enable learning safely.

Once an erase call returns `true`, its vendor request is committed. A concurrent
sleep or reset waits for that request to complete, and
`CWSLDeleteSucceeded` is emitted only after the vendor completion callback.
`CWSLDeleteFailed` is reserved for a committed delete that cannot complete;
requests rejected before submission return `false` without an event.

The programmatic API does not require the official voice-guided learning
prompts. Sketches can provide feedback through Serial, LEDs, displays, or the
`ChipIntelliAudio` library in response to CWSL events. Learned recognition is
also forwarded through the normal `ChipIntelliASR` result path.
