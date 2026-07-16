#include "Arduino.h"

extern "C" void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value) {
    for (uint8_t i = 0; i < 8; ++i) {
        uint8_t bitIndex = bitOrder == LSBFIRST ? i : 7 - i;
        digitalWrite(dataPin, (value >> bitIndex) & 1U);
        digitalWrite(clockPin, HIGH);
        digitalWrite(clockPin, LOW);
    }
}

extern "C" uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder) {
    uint8_t value = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        digitalWrite(clockPin, HIGH);
        uint8_t bitIndex = bitOrder == LSBFIRST ? i : 7 - i;
        value |= static_cast<uint8_t>(digitalRead(dataPin) << bitIndex);
        digitalWrite(clockPin, LOW);
    }
    return value;
}
