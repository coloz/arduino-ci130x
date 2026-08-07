# CI13XX board examples

This metadata-only library groups the core and board examples so Arduino IDE
exposes them under **File > Examples > CI13XX**. GPIO, analog, PWM, interrupt,
serial, peripheral-resource, and FreeRTOS APIs remain part of the Arduino core and vendor SDK; the
examples include `Arduino.h` directly.

The examples support the CI1302 / CI-D02GS02S, CI1303 / CI-D03GS02S, generic
CI1306 and CI-D06GT01D development-board profiles. The dedicated CI-D06GT01D
profile defaults to dual microphones with the non-AEC Standard ASR algorithm;
the generic CI1306 profile defaults to a single microphone with AEC. Both expose
analog single/dual and PDM digital single/dual choices in the Arduino
**Microphone Input** menu. Analog dual-microphone and all PDM modes must be paired with
a non-AEC algorithm profile. PDM uses PB7 for data and PC0 for clock; those pads
remain system-owned while PDM input is selected. The development-board
profile also exposes the active-high onboard PD1 LED as `LED_BUILTIN`; the generic
chip profiles require an external LED and current-limiting resistor. The SSOP24
variants use PC4 where the CI1306 examples use PB3/PB4; see each sketch for
wiring and resource conflicts.

`BufferedSerial` demonstrates the interrupt-backed UART, Arduino frame-format
constants, and error counters. `ResourceOwnership` demonstrates how the core
rejects the shared-pad conflict between Wire and Serial1 before changing any
mux register.
