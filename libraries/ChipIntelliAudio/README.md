# ChipIntelliAudio

`ChipIntelliAudio` wraps the CI13XX vendor SDK prompt player in an Arduino API.
It plays audio stored in the `voice.bin` image that is flashed as part of the
firmware.

Use the global `ChipIntelliAudio` object. The SDK player and its completion and
mute hooks are process-wide, so the class deliberately cannot be constructed
or copied as an independent second player.

The library supports:

- playing audio by voice ID;
- playing a configured prompt by command ID, command text, or semantic ID;
- stopping playback, checking playback status, adjusting the volume, and
  reliably muting/unmuting output;
- receiving a callback when the SDK finishes processing a prompt request;
- interrupting the current prompt or adding a new prompt to the playback queue.

This library is not a TTS engine. The string overload of `playCommand()` only
looks up a configured command; it does not convert arbitrary text to speech.
The current version also cannot open WAV or MP3 files directly from an SD card
or filesystem.

## Quick Start

```cpp
#include <ChipIntelliAudio.h>

constexpr uint16_t kVoiceId = 1;

void setup() {
  Serial.begin(115200);

  // begin() waits for the SDK and audio hardware to initialize. There is no
  // need to call ChipIntelliASR.begin() separately or add a fixed delay.
  if (!ChipIntelliAudio.begin()) {
    Serial.println("Audio initialization failed");
    return;
  }

  ChipIntelliAudio.setVolume(70);

  if (!ChipIntelliAudio.playVoice(kVoiceId)) {
    Serial.println("Playback request failed");
  }
}

void loop() {}
```

## Initialization and Shutdown

### `bool begin()`

Call `begin()` before any playback operation. When necessary, it starts the
vendor SDK shared by the Arduino firmware and other ChipIntelli libraries. It
then waits for the flash resources, audio task, codec, and amplifier to become
ready, while also avoiding interference from the SDK startup prompt. Normal use
does not require an additional `delay(2000)`.

Initialization waits for up to 10 seconds. It returns `true` on success and
`false` on failure or timeout. Calling `begin()` again after successful
initialization immediately returns `true`. Do not issue playback requests if it
returns `false`.

If the sketch also uses `ChipIntelliASR`, both libraries share the same SDK.
You do not need to call `ChipIntelliASR.begin()` just to play audio.

### `void end()`

`end()` clears the playback-completion callback, submits a stop request, and
returns this library to the uninitialized state. It does not shut down the
underlying SDK shared with other ChipIntelli libraries. Call `begin()` again
before playing another prompt. As with `stop()`, the SDK does not report whether
playback reached idle before its bounded wait expired.

## Audio Resources and IDs

Voice, command, and semantic IDs come from the model and command resource
package used by the current project. If the project does not contain resource
files, the build hook copies the platform defaults into the sketch's `recursos`
directory. A normal compiled Arduino upload image includes `voice.bin`,
`asr.bin`, `dnn.bin`, and the other files from that directory.

To change the prompts, replace `recursos/voice.bin` in the sketch and use IDs
that match that resource package. The value `1` in `playVoice(1)` is only an
example and is not guaranteed to exist in every custom resource package.

## Playback API

| API | Purpose |
| --- | --- |
| `playVoice(voiceId, interruptCurrent)` | Play the 16-bit voice ID from `voice.bin` |
| `playCommand(commandId, optionIndex, interruptCurrent)` | Play the prompt associated with a command ID |
| `playCommand(commandText, optionIndex, interruptCurrent)` | Look up a configured command by text and play its prompt |
| `playSemantic(semanticId, optionIndex, interruptCurrent)` | Play the prompt associated with a 32-bit semantic ID |
| `stop()` | Submit a stop request; treated as already stopped if uninitialized |
| `isPlaying()` | Return `true` while a prompt is playing |
| `isReady()` | Return whether `begin()` completed successfully |
| `setMuted()`, `mute()`, `unmute()` | Mute or restore the previously requested volume |
| `isMuted()` | Return the wrapper's current mute state |

A `true` return value from any `play...()` method only means that the SDK's
outer prompt layer accepted the request; it does not prove that resource
lookup or playback later succeeded. The method returns `false` for arguments
that this wrapper can reject, when the library is not initialized, or when the
SDK rejects the request immediately. `stop()` likewise cannot report whether
the SDK reached idle before its internal bounded wait expired.

`interruptCurrent` defaults to `true`, which interrupts the current prompt
before playing the new one. Pass `false` to add the request to the SDK prompt
queue:

```cpp
ChipIntelliAudio.playVoice(1);         // Play immediately
ChipIntelliAudio.playVoice(2, false);  // Play after the previous prompt
```

`optionIndex` defaults to `-1` for the command and semantic APIs. This lets the
SDK use the prompt option configured in the resource package. Pass a
non-negative index only when the package defines multiple prompts for the same
command and you know the required option number.

## Volume

```cpp
ChipIntelliAudio.setVolume(70);
uint8_t current = ChipIntelliAudio.volume();
```

The valid range for `setVolume()` is `0` through `100`, with a maximum of
`100`:

- `0` mutes the output;
- `1` through `100` select progressively higher gain settings;
- `101` through `255` are clamped to `100`.

The parameter type is `uint8_t`; callers should pass a value directly within
the `0` through `100` range. The underlying codec quantizes nonzero values to a
limited number of DAC gain steps, so the value is not a linear acoustic
loudness percentage and adjacent values may produce the same physical gain.
`volume()` returns the current volume setting, also in the `0` through `100`
range.

`mute()` preserves that requested setting and applies zero output gain;
`unmute()` restores it. Calling `setVolume()` while muted changes the volume
that will be restored without producing sound. An Arduino SDK hook also forces
SDK-owned volume changes to remain at zero until `unmute()`, so voice/UART
volume commands cannot accidentally break mute. This behavior intentionally
does not call the vendor `audio_play_set_mute()` function because its SDK
V2.7.14 implementation does not control the codec.

The SDK also declares pause/resume and playback-speed functions, but they are
not exposed here: prompt resume is not reliable in the default player, and
speed control is compiled out of the standard ASR profile. Keeping those
operations out of the public API avoids reporting success for unsupported
hardware behavior.

## Playback-Completion Callback

```cpp
volatile bool playbackFinished = false;

void onPlaybackFinished(void *context) {
  (void)context;
  playbackFinished = true;
}

void setup() {
  if (!ChipIntelliAudio.begin()) {
    return;
  }

  // Register the callback before issuing the playback request.
  ChipIntelliAudio.onFinished(onPlaybackFinished);
  ChipIntelliAudio.playVoice(1);
}

void loop() {
  if (playbackFinished) {
    playbackFinished = false;
    // Perform printing, communication, or other time-consuming work here.
  }
}
```

The SDK originally invokes completion while holding its prompt mutex. The
Arduino integration records that event and invokes the user callback from a
post-unlock SDK hook, so prompt APIs are never re-entered while that mutex is
held. The callback has no fixed task context: an immediately rejected request
can notify from the calling sketch task, while asynchronous completion normally
notifies from an SDK audio task. Keep it short and avoid `delay()`, playback
control, waiting for locks, Flash writes, large prints, or other time-consuming
work. Prefer setting a `volatile` flag or sending a non-blocking queue message,
then handling it from `loop()`. The notification means that the SDK finished
processing the prompt request; the SDK does not provide a success/failure
status, so it may also follow an interrupted request or a late resource lookup
failure. The `context` passed to `onFinished()` is forwarded unchanged. Call
`onFinished(nullptr)` to clear the callback.

## Examples

- `PlayVoiceId` initializes the player, plays a voice ID, and uses a safe flag
  to handle the playback-completion event.
- `PromptControl` accepts commands from a serial monitor at `115200` baud and
  demonstrates all four prompt lookup modes, stopping playback, checking
  status, and adjusting volume.

## Troubleshooting No Audio

1. Check whether `begin()` returns `true` and whether the serial monitor reports
   an initialization failure.
2. Check whether `play...()` returns `true`. A `false` result means the wrapper
   or the SDK's outer prompt layer rejected the request immediately; a `true`
   result still does not prove that later resource lookup succeeded.
3. Confirm that the ID belongs to the `voice.bin` in the sketch's `recursos`
   directory, rather than to another command resource package.
4. Confirm that the volume is not `0`, then check the speaker, amplifier enable,
   and board audio-output connections.
5. With the fixed library, do not rely on the temporary workaround of calling
   `ChipIntelliASR.begin()` followed by a fixed delay. Check only the result of
   `ChipIntelliAudio.begin()`.
