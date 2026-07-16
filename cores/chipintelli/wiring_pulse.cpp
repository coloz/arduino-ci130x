#include "Arduino.h"

extern "C" unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout) {
    const unsigned long start = micros();
    while (digitalRead(pin) == state) {
        if (micros() - start >= timeout) return 0;
    }
    while (digitalRead(pin) != state) {
        if (micros() - start >= timeout) return 0;
    }
    const unsigned long pulseStart = micros();
    while (digitalRead(pin) == state) {
        if (micros() - start >= timeout) return 0;
    }
    return micros() - pulseStart;
}

extern "C" unsigned long pulseInLong(uint8_t pin, uint8_t state, unsigned long timeout) {
    return pulseIn(pin, state, timeout);
}
