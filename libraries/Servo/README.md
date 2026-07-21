# Servo for CI13XX

This library implements the common Arduino `Servo` API with the six CI13XX
hardware PWM channels. It generates a 50 Hz frame and accepts angles or pulse
widths:

```cpp
#include <Servo.h>

Servo servo;

void setup() {
  servo.attach(11);
  servo.write(90);
}

void loop() {}
```

Implemented APIs are `attach()`, `detach()`, `write()`,
`writeMicroseconds()`, `read()`, `readMicroseconds()` and `attached()`.
Default pulses are 544 through 2400 microseconds with a 1500-microsecond
center. `attach(pin, min, max)` accepts custom bounds below the 20 ms frame.

Only pins marked with `PIN_CAP_PWM` can be attached. Several package pins map
to the same underlying PWM channel; the library rejects a second Servo that
would claim an already attached channel. The SSOP24 CI1302/CI1303 profiles
provide five usable PWM channels because their PWM5 pad is reserved for the
external crystal. CI1306 exposes pins for all six channels.

Servo owns its PWM channel while attached. Do not use `analogWrite()`,
`tone()` or another peripheral mux on a pin or channel owned by Servo. The
library detects conflicts between Servo objects, but it cannot detect PWM use
started directly by other code. `detach()` stops the channel and drives the
former signal pin low.

The PWM uses the CI13XX SRC clock. Duty scaling is calculated from the runtime
clock so the vendor driver's 32-bit period-times-duty operation cannot
overflow. Resolution is approximately 1 to 2 microseconds in the packaged
12.288/16 MHz clock profiles.

Power a servo motor from a suitable external supply and connect its ground to
the CI13XX board ground. Do not power the motor from a GPIO pin, and verify the
board supply current and signal-voltage requirements before connecting it.

The included `Sweep` example uses PC4/PWM0 on CI1302/CI1303 and PB3/PWM4 on
CI1306. Compilation covers all three variants; physical servo timing and load
testing still require hardware validation.
