# ChipIntelliCWSL

`ChipIntelliCWSL` exposes the CI130X command-word self-learning engine to an
Arduino sketch. Select a **Tools > Algorithm Profile > CWSL Command-Word Learning**
profile before compiling.
`begin()` returns `false` under the
standard offline-ASR profile.

Learning is asynchronous. `learnCommand()` or `learnWakeWord()` starts audio
capture and returns immediately. Poll events with `read()` or install an
`onEvent()` callback. Callbacks execute in a vendor SDK task and must not
block; use the event queue for longer work from `loop()`.
Control calls made synchronously from `onEvent()` can return `false` while the
current event owns the operation transition. Queue those requests and execute
them from `loop()` instead.
`CWSLRecordingStarted` means that the bridge successfully queued the vendor
record-start request; this vendor binary does not expose a physical-capture
start callback.
All CWSL APIs use task-only SDK services and must be called from `setup()`,
`loop()`, or another RTOS task—not from an ISR or hardware-timer callback.

`BasicLearning` is the size-constrained starting point for CI1302 CWSL builds
on Linux/macOS. The richer `SerialLearning` example fits CI1303 and CI1306;
Windows LTO builds can also fit it on CI1302.

```cpp
#include <ChipIntelliCWSL.h>

void setup() {
  ChipIntelliCWSL.begin();
  ChipIntelliCWSL.learnCommand(2);
}

void loop() {
  ChipIntelliCWSLEvent event;
  while (ChipIntelliCWSL.read(event)) {
    // Handle learning, deletion, and learned-word recognition events.
  }
}
```

The command ID passed to a learning call must already exist in the active
`cmd_info` resource. `learnWakeWord()` accepts only a command marked as a wake
word, and `learnCommand()` accepts only a non-wake command. CWSL learns a new
spoken phrase that maps to that command; it does not create new command
metadata or text. Command IDs must be no greater than 65535 because the vendor
recognition callback reports a 16-bit command ID; group IDs must be no greater
than 255 because the persisted template stores an 8-bit group. Missing IDs,
type mismatches, values outside these ranges, and the packaged voice-control
IDs 199 through 208 return `false`.

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
