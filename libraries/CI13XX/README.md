# CI13XX board examples

This library groups the core and board examples so Arduino IDE exposes them
under **File > Examples > CI13XX**. `CI13XX.h` only publishes the package
version; GPIO, analog, PWM, interrupt, and serial APIs remain part of the
Arduino core.

All examples target the current CI-D06GT01D / CI1306 profile. The board has no
Arduino-controlled built-in LED, so LED examples require an external LED and
current-limiting resistor. See each sketch for wiring and resource conflicts.
