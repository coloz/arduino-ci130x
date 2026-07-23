# ChipIntelliWatchdog

`ChipIntelliWatchdog` is a small Arduino wrapper around the official CI130X
independent-watchdog (`ci130x_iwdg`) driver. It enables the peripheral clock,
routes watchdog reset to the whole system, converts milliseconds using the
runtime SRC clock, and exposes start, feed, and stop operations.

```cpp
#include <ChipIntelliWatchdog.h>

void setup() {
  Serial.begin(115200);
  ChipIntelliWatchdog.begin(3000);
}

void loop() {
  ChipIntelliWatchdog.feed();
  delay(1000);
}
```

## API

- `begin(timeoutMs)` configures and starts IWDG. It returns `false` for zero,
  unavailable-clock, or out-of-range timeouts. Calling it again safely
  reconfigures the same hardware watchdog.
- `feed()` (alias `reset()`) clears a pending warning and reloads the counter.
  It returns `false` if this wrapper has not started IWDG.
- `end()` (alias `disable()`) clears a pending stage, stops IWDG, restores the
  no-reset route, disables its peripheral clock, and is idempotent.
- `isRunning()`, `timeoutMs()`, `reloadCount()`, `counterClockHz()`, and
  `maximumTimeoutMs()` report the wrapper configuration.
- `lastError()` and `errorString()` report validation or state errors.

Call the control methods from normal Arduino/FreeRTOS task context, not from an
interrupt handler. ISR calls fail safely with `Error::InterruptContext`. The
wrapper uses a task critical section to keep the SDK's unlock/write/lock
register transaction atomic.

## CI130X timeout semantics

The SDK programs the IWDG reload value from `SRC_CLK / 16`. The `timeoutMs`
argument is therefore **one hardware countdown stage**, matching the vendor
driver and sample calculation. With both interrupt and system reset enabled,
the first unserviced expiration raises the watchdog warning and reloads the
counter; reset follows after a second consecutive expiration. A 3000 ms stage
therefore normally resets the chip after approximately 6000 ms without any
feed. Clock tolerance also affects the exact interval.

The wrapper deliberately does not install an interrupt handler: its purpose is
recovery by reset, and the official Arduino startup already supplies the weak
SDK handler. `feed()` clears either a normal countdown or the pending first
stage.

IWDG is a chip-wide singleton and does not use a pin or a multiplexed timer, so
it is not registered with `PeripheralManager`. Do not mix this wrapper with
direct calls to `iwdg_init()`, `iwdg_open()`, or `iwdg_close()`; direct calls
can make the wrapper's reported state stale.

When the official audio-input pipeline is running, its SDK task also feeds the
same IWDG. In that configuration the watchdog detects a stalled system/audio
pipeline, but it cannot be used as a strict monitor of only the Arduino
`loop()` task.
