# ChipIntelliASR

`ChipIntelliASR` receives the good-result path of the CI13XX SDK without
replacing its ASR tasks or generated command handling. Call `begin()` from
`setup()`, then consume copied results from `loop()` with `read()`.

The queue holds three pending results (four slots with one reserved for SPSC
synchronization), and each command string is copied into a 64-byte field.
`droppedResults()` reports queue overflow. `onResult()` is optional; its
callback runs directly in the SDK message task, so it must not block.

The SDK's canonical result hook exposes command/semantic IDs, score and text
for wake words and ordinary commands. `frames` is preserved when the normal
command path provides the original message; it is `0` for results such as wake
words where the canonical SDK hook does not expose a frame count.
