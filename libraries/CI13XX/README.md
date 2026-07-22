# CI13XX board examples

This metadata-only library groups the core and board examples so Arduino IDE
exposes them under **File > Examples > CI13XX**. GPIO, analog, PWM, interrupt,
serial, and FreeRTOS APIs remain part of the Arduino core and vendor SDK; the
examples include `Arduino.h` directly.

The examples support the CI1302 / CI-D02GS02S, CI1303 / CI-D03GS02S and CI1306
/ CI-D06GT01D profiles. These boards have no Arduino-controlled built-in LED,
so LED examples require an external LED and current-limiting resistor. The
SSOP24 variants use PC4 where the CI1306 examples use PB3/PB4; see each sketch
for wiring and resource conflicts.
