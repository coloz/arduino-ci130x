# ChipIntelliASR

`ChipIntelliASR` receives the good-result path of the CI13XX SDK without
replacing its ASR tasks or generated command handling. Call `begin()` from
`setup()`, check its return value, then consume copied results from `loop()`
with `read()`.

```cpp
if (!ChipIntelliASR.begin()) {  // waits up to 10 seconds by default
  Serial.println("ASR initialization failed or timed out");
}
```

`begin(timeoutMs)` starts the shared vendor SDK once and waits until its
initialization task has created the resources required by the default ASR flow
and the audio input path reports that it has started. It returns `false` on
task/resource initialization failure or timeout.
A timeout does not stop an SDK task that is already starting; a later call to
`begin()` can attach after that shared task becomes ready.

The queue holds three pending results, and each command string is copied into
a 64-byte field. `Result::textTruncated` is `true` when the SDK text did not
fit. `droppedResults()` reports queue overflow. `onResult()` is optional and
continues to receive new results even while the polling queue is full. Its
callback runs directly in the SDK message task, so it must not block or call
code that waits for ASR/audio work.

The SDK's canonical result hook exposes command/semantic IDs, score and text
for wake words and ordinary commands. `frames` is preserved when the normal
command path provides the original message; it is `0` for results such as wake
words where the canonical SDK hook does not expose a frame count.

`end()` only detaches this library's result listener and clears its queue. It
does not shut down the shared SDK because audio and other SDK services may be
using it.

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
