# ChipIntelliAudio

`ChipIntelliAudio` exposes the CI13XX SDK prompt player to Arduino sketches.
It plays audio that is already provisioned in the board's `voice.bin` image:

- `playVoice()` selects a voice file by voice ID;
- `playCommand()` selects the prompt assigned to a command ID or command text;
- `playSemantic()` selects the prompt assigned to a semantic ID;
- `stop()`, `isPlaying()`, `setVolume()` and `onFinished()` provide basic
  playback control.

```cpp
#include <ChipIntelliAudio.h>

void setup() {
  ChipIntelliAudio.begin();
  ChipIntelliAudio.setVolume(70);
  ChipIntelliAudio.playVoice(1);
}

void loop() {}
```

Voice, command and semantic IDs come from the model/command package used to
build the board's full firmware. Replace the example ID with one that exists in
that package. An Arduino user-code upload updates only the user-code partition;
it does not install or replace `voice.bin`.

The `optionIndex` argument used by command and semantic playback defaults to
`-1`, which lets the SDK select the configured prompt. `interruptCurrent=true`
stops the current prompt before starting the new one. Set it to `false` to add
the request to the SDK's prompt queue.

`onFinished()` runs in the SDK audio/prompt task. Keep the callback short; set a
flag or enqueue work for `loop()` instead of blocking in the callback.

The `PlayVoiceId` example demonstrates completion notification. The
`PromptControl` example exercises every lookup mode and the basic controls from
the serial monitor without starting playback until a command is received.

## Scope

This library does not make a command string into speech: the string overload
looks up a configured command and plays its assigned prompt. It also does not
open arbitrary WAV/MP3 files. The current Arduino platform has no supported
filesystem or SD-card data path wired to the vendor player, while the
provisioned `voice.bin` prompt path is part of the validated SDK baseline.
