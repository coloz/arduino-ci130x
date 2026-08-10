# ChipIntelliAudio

`ChipIntelliAudio` wraps the CI13XX vendor SDK prompt player in an Arduino API.
It plays audio stored in the `voice.bin` image that is flashed as part of the
firmware.

Use the global `ChipIntelliAudio` object. The SDK player and its completion and
mute hooks are process-wide, so the class deliberately cannot be constructed
or copied as an independent second player.

The library supports:

- playing audio by voice ID;
- playing one copied sequence of up to 24 voice IDs with one completion event;
- parsing decimal strings passed directly to `playVoice()` and composing them
  in any of 15 supported languages from IDs beginning at 300;
- playing the built-in beep prompt one to sixteen times;
- playing a configured prompt by command ID, command text, or semantic ID;
- stopping playback, checking playback status, adjusting the volume, and
  reliably muting/unmuting output;
- receiving a callback when the SDK finishes processing a prompt request;
- interrupting the current prompt or adding a new prompt to the playback queue.

This library is not a TTS engine. The string overload of `playCommand()` only
looks up a configured command; it does not convert arbitrary text to speech.
The variable-number APIs only concatenate clips that were generated in
advance and stored in `voice.bin`. The current version also cannot open WAV or
MP3 files directly from an SD card or filesystem.

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

`end()` clears the playback-completion callback, enqueues a stop request, and
returns this library to the uninitialized state. It does not shut down the
underlying SDK shared with other ChipIntelli libraries. Call `begin()` again
before playing another prompt.

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
| `playVoice(voiceId, interruptCurrent)` | Numeric argument: play a 16-bit voice ID from `voice.bin` |
| `playVoice(numberText, interruptCurrent)` | String argument: parse and speak a decimal number |
| `playVoiceSequence(voiceIds, count, interruptCurrent)` | Play 1 to 24 voice IDs as one logical prompt |
| `playNumber(value, voiceIds)` | Compose and play a signed 32-bit Mandarin integer |
| `playFixedPoint(value, fractionalDigits, voiceIds)` | Compose and play a deterministic fixed-point value |
| `buildNumberVoiceSequence(...)` | Build and inspect an integer voice-ID sequence |
| `buildFixedPointVoiceSequence(...)` | Build and inspect a fixed-point voice-ID sequence |
| `playBeep(count)` | Play the built-in beep prompt 1 to 16 times |
| `playCommand(commandId, optionIndex, interruptCurrent)` | Play the prompt associated with a command ID |
| `playCommand(commandText, optionIndex, interruptCurrent)` | Look up a configured command by text and play its prompt |
| `playSemantic(semanticId, optionIndex, interruptCurrent)` | Play the prompt associated with a 32-bit semantic ID |
| `stop()` | Enqueue a stop request; treated as already stopped if uninitialized |
| `isPlaying()` | Return `true` while playback is active or requests are queued |
| `isReady()` | Return whether `begin()` completed successfully |
| `setMuted()`, `mute()`, `unmute()` | Mute or restore the previously requested volume |
| `isMuted()` | Return the wrapper's current mute state |

A `true` return value from any `play...()` method means that the library's
FreeRTOS playback queue copied the request. Resource lookup and playback happen
later in the playback task, so `true` does not prove that they will succeed.
The method returns `false` for invalid arguments, an uninitialized library, or
a full queue. Playback calls and `stop()` never wait for the SDK player.

`interruptCurrent` defaults to `true`, which puts the request on the
high-priority interrupt queue. Pass `false` to add it to the ordered playback
queue. The dedicated task does not start the next ordered request until the
complete current request has finished. The ordered queue holds eight pending
requests and the interrupt queue holds four; enqueueing returns `false` when
the selected queue is full:

```cpp
ChipIntelliAudio.playVoice(1);         // Play immediately
ChipIntelliAudio.playVoice(2, false);  // Play after the previous prompt
```

`optionIndex` defaults to `-1` for the command and semantic APIs. This lets the
SDK use the prompt option configured in the resource package. Pass a
non-negative index only when the package defines multiple prompts for the same
command and you know the required option number.

`playBeep()` plays one beep by default. Pass a value from `1` through `16` to
select the count, for example `ChipIntelliAudio.playBeep(3)`. The method uses
the resource package's special `<beep>` command, interrupts the current prompt,
and emits one completion callback after the whole group. The standard resource
package includes this command; the method returns `false` if a custom package
does not. It also returns `false` when `count` is zero or greater than `16`.

## Variable Number Playback

Variable-number playback supports Chinese, English, Japanese, Korean, Russian,
Spanish, Thai, German, Indonesian, Vietnamese, French, Portuguese, Persian,
Turkish, and Arabic. Each language has its own voice resource set beginning at
ID 300, so only one language occupies those IDs in a firmware image. Select it
before the library include; Chinese is the default:

```cpp
#define CHIPINTELLI_LANGUAGE CHIPINTELLI_LANGUAGE_EN
#include <ChipIntelliAudio.h>

ChipIntelliAudio.playVoice("300");   // three hundred
ChipIntelliAudio.playVoice("-1.5");  // minus one point five
ChipIntelliAudio.playVoice("0.02");  // zero point zero two
ChipIntelliAudio.playVoice("0.02", false);  // Queue the complete number
```

The selector values are `_ZH`, `_EN`, `_JA`, `_KO`, `_RU`, `_ES`, `_TH`, `_DE`,
`_ID`, `_VI`, `_FR`, `_PT`, `_FA`, `_TR`, and `_AR` with the full
`CHIPINTELLI_LANGUAGE_` prefix. When the linked program uses the string
overload, Arduino post-build asks `citool-cli generate` to add the complete
token inventory for the selected language. No `VOICE300`-style macros are
needed.

`interruptCurrent` defaults to `true`. Pass `false` to queue the complete
number after the prompt that is currently playing. The playback task submits
all tokens for one number to the SDK as a single logical sequence, and does not
dequeue the next number until that whole sequence has finished. The complete
number emits one completion callback.
An integer argument retains the original meaning: `playVoice(300)` plays raw
voice ID 300, while `playVoice("300")` speaks the number three hundred.

The string syntax permits surrounding ASCII whitespace, an optional `+` or
`-`, and one decimal point. Scientific notation is rejected. Fractional digits
are spoken individually, including zeroes. Invalid input, an integer part
larger than `uint32_t`, or a result longer than 24 clips returns `false`.
Negative textual zero is spoken as zero without a minus token.

The lower-level `NumberVoiceIds` API retains its Mandarin composition rules.
To use a custom Mandarin ID set, map it and call:

```cpp
const ChipIntelliAudioClass::NumberVoiceIds numberVoices = {
    {300, 301, 302, 303, 304, 305, 306, 307, 308, 309},
    310,  // 十
    311,  // 百
    312,  // 千
    313,  // 万
    314,  // 亿
    315,  // 负
    316,  // 点
};

ChipIntelliAudio.playNumber(-10010, numberVoices);      // 负一万零一十
ChipIntelliAudio.playFixedPoint(235, 1, numberVoices);  // 二十三点五
```

`playFixedPoint()` avoids floating-point formatting: `(235, 1)` means `23.5`,
while `(2300, 2)` means `23.00`, including the two trailing zero tokens. The
fractional digit count must be from `0` through `9`.

Use `buildNumberVoiceSequence()` or `buildFixedPointVoiceSequence()` when the
generated ID sequence must be inspected or adjusted before passing it to
`playVoiceSequence()`. A sequence contains at most 24 clips. Its optional
`interruptCurrent` argument only controls how the group starts, and the group
produces one completion event.

See [`VARIABLE_VOICE_TEXTS.md`](VARIABLE_VOICE_TEXTS.md) for every language's
exact ID table, composition rules, examples, and clip-production guidance.

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
Arduino integration records that event, waits for the post-unlock hook, and
wakes the dedicated playback task. The user callback therefore always runs
from the `ChipIntelliAudio` playback task, once per logical request rather than
once per number token. Keep it short and avoid `delay()`, waiting for locks,
Flash writes, large prints, or other time-consuming work. Prefer setting a
`volatile` flag or sending a non-blocking queue message, then handling it from
`loop()`. The notification does not include a success/failure status, so it may
also follow an interrupted request or a late resource lookup failure. The
`context` passed to `onFinished()` is forwarded unchanged. Call
`onFinished(nullptr)` to clear the callback.

## Examples

- `PlayVoiceId` initializes the player, plays a voice ID, and uses a safe flag
  to handle the playback-completion event.
- `VariableNumber` queues an integer and two floating-point values while
  `loop()` continues running; playback uses reusable tokens from ID 300.
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
