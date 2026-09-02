# CI13XX board examples

This metadata-only library groups the core and board examples so Arduino IDE
exposes them under **File > Examples > CI13XX**. GPIO, analog, PWM, interrupt,
serial, peripheral-resource, and FreeRTOS APIs remain part of the Arduino core and vendor SDK; the
examples include `Arduino.h` directly.

The examples support the CI1302 / CI-D02GS02S, CI1303 / CI-D03GS02S, generic
CI1306, CI-D06GT01D and easyVoice 1306 dev profiles. The dedicated CI-D06GT01D
profile defaults to dual microphones with the non-AEC Standard ASR algorithm;
the generic CI1306 and easyVoice profiles default to a single microphone with
AEC. The CI1306 profiles expose analog single/dual and PDM digital single/dual
choices in the Arduino
**Microphone Input** menu. Analog dual-microphone and all PDM modes must be paired with
a non-AEC algorithm profile. PDM uses PB7/PC0 on generic CI1306 and
CI-D06GT01D, and PC3/PC2 on easyVoice; the selected pads remain system-owned
while PDM input is enabled. CI-D06GT01D exposes its PD1 LED and easyVoice
exposes its PD4 LED as `LED_BUILTIN`; generic chip profiles require an external
LED and current-limiting resistor. The SSOP24 variants use PC4 where the CI1306
examples use PB3/PB4; see each sketch for wiring and resource conflicts.

`BufferedSerial` demonstrates the interrupt-backed UART, Arduino frame-format
constants, and error counters. `ResourceOwnership` demonstrates how the core
rejects shared-pad conflicts before changing any mux register. On easyVoice,
Wire uses PB3/PB4 and therefore does not share the Serial1 PB7/PC0 pads.
