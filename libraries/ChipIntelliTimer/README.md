# ChipIntelliTimer

This library provides two timing APIs for CI1302, CI1303 and CI1306:

- `ChipIntelliTimer` drives TIMER0, TIMER1 or TIMER2. Its callback runs in
  interrupt context and can provide microsecond periods. TIMER3 is reserved by
  the official BLE stack in the current Arduino profile.
- `Ticker` (also available as `ChipIntelliTicker`) uses a FreeRTOS software
  timer. Its callback runs in the timer-service task and does not consume a
  hardware timer.

## Hardware timer

```cpp
#include <ChipIntelliTimer.h>

ChipIntelliTimer timer;
volatile bool elapsed = false;

void onTimer() {
  elapsed = true;
}

void setup() {
  timer.begin(100000, onTimer);  // 100 ms, repeating, automatic channel
}
```

`begin(periodMicros, callback, repeat)` automatically selects a free timer from
TIMER0 through TIMER2. Use `begin(timerNumber, periodMicros, callback, repeat)`
to request one of those timers explicitly. Although TIMER3 is a valid hardware
timer number, the fixed `USE_BLE` profile uses it as the RF time base, so an
explicit TIMER3 request returns `false` with `Error::ResourceBusy`. Other calls
return `false` if the period is invalid or the resource is already owned by
infrared or another timer object. Callback-with-argument overloads are also
provided.

`start()`, `stop()`, `restart()`, `setPeriod()`, `end()`, `running()`,
`timerNumber()` and `lastError()` are available. `end()` disables the interrupt
and releases the timer resource.

Hardware timer callbacks execute directly in an interrupt. Do not call
`Serial`, `delay()`, heap allocation, flash writes, or blocking/FreeRTOS APIs
from them. Set a `volatile` flag or counter and perform that work from
`loop()`. The timer's own `begin()`, `end()`, `start()`, `stop()`, `restart()`
and `setPeriod()` methods are also task-only: an ISR call is rejected with
`Error::InterruptContext`. Do not destroy a timer object from its callback.
Concurrent task control of the same object is rejected with `Error::Busy`.
TIMER0 through TIMER2 are separate from both the BLE TIMER3 time base and the
machine timer used for the FreeRTOS scheduler and Arduino `millis()`.

The library calculates the hardware divider from the runtime APB clock and
uses the smallest divider that fits the requested interval in the 32-bit
counter. The effective interval is rounded to the nearest hardware clock
tick. The period is calculated from the APB clock when `begin()` or
`setPeriod()` runs; a future low-power mode that changes APB frequency must
stop and reconfigure active timers.

## Ticker

```cpp
#include <Ticker.h>

Ticker ticker;

void poll() {}

void setup() {
  ticker.attach_ms(250, poll);
}
```

`attach()` and `once()` accept seconds; `attach_ms()` and `once_ms()` accept
milliseconds. `detach()`, `active()` and `lastError()` are also provided, with
no-argument and `void *` callback forms. At most eight Ticker objects can be
scheduled simultaneously.

Ticker uses the SDK's FreeRTOS software-timer service. The configured kernel
tick rate is 500 Hz, so intervals have 2 ms resolution and are rounded up to
the next tick. Callbacks should remain short: blocking a Ticker callback also
delays SDK software timers used by audio, keys, and other services. Create and
control Tickers from `setup()`, `loop()`, or another task, not from an ISR;
ISR calls fail safely with `Ticker::Error::InterruptContext`.
Calling `detach()` or attaching a new interval from the Ticker callback is
supported. Do not destroy the `Ticker` object from its callback. If the SDK's
five-entry timer command queue is temporarily full, `lastError()` reports
`Ticker::Error::QueueFull`; callbacks remain disabled and a later `detach()` or
attach call from a task safely retries the pending cleanup. Each FreeRTOS
timer-queue operation waits at most 100 ms for queue space. Concurrent control
of the same object is rejected with `Ticker::Error::Busy`.

See the `HardwareTimer` and `Ticker` examples; both diagnostic serial ports are
initialized at 115200 baud.
