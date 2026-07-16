#ifndef Arduino_h
#define Arduino_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pgmspace.h"

// Keep every Arduino translation unit on the same board/algorithm profile as
// the precompiled SDK archive. The platform adds the SDK include directory.
#include "user_config.h"

#ifdef __cplusplus
#include <cmath>
#include <algorithm>
#endif

#define ARDUINO_CORE_VERSION_MAJOR 0
#define ARDUINO_CORE_VERSION_MINOR 1
#define ARDUINO_CORE_VERSION_PATCH 0

#define HIGH 0x1
#define LOW  0x0

#define INPUT           0x01
#define OUTPUT          0x03
#define INPUT_PULLUP    0x05
#define INPUT_PULLDOWN  0x09
#define OUTPUT_OPEN_DRAIN 0x13

#define CHANGE  1
#define FALLING 2
#define RISING  3
#define ONLOW   4
#define ONHIGH  5

#define LSBFIRST 0
#define MSBFIRST 1

#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define EULER 2.718281828459045235360287471352

#define SERIAL 0x0
#define DISPLAY 0x1

#define SERIAL_8N1 0x800001c

#define bit(b) (1UL << (b))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

#ifdef __cplusplus
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif
#else
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#endif

typedef bool boolean;
typedef uint8_t byte;
typedef uint16_t word;

typedef void (*voidFuncPtr)(void);
typedef void (*voidFuncPtrArg)(void *);

#ifdef __cplusplus
extern "C" {
#endif

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);
void digitalToggle(uint8_t pin);

int analogRead(uint8_t pin);
void analogReadResolution(uint8_t bits);
void analogWrite(uint8_t pin, int value);
void analogWriteResolution(uint8_t bits);
void analogWriteFrequency(uint32_t frequency);

unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield(void);
void interrupts(void);
void noInterrupts(void);

unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout);
unsigned long pulseInLong(uint8_t pin, uint8_t state, unsigned long timeout);

void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value);
uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder);

void attachInterrupt(uint8_t pin, voidFuncPtr callback, int mode);
void attachInterruptArg(uint8_t pin, voidFuncPtrArg callback, void *arg, int mode);
void detachInterrupt(uint8_t pin);

void tone(uint8_t pin, unsigned int frequency, unsigned long duration);
void noTone(uint8_t pin);

void setup(void);
void loop(void);

#ifndef CHIPINTELLI_ASR_CORE_HOOK_TYPES
#define CHIPINTELLI_ASR_CORE_HOOK_TYPES
typedef struct chipintelli_asr_result_t {
    uint16_t command_id;
    uint32_t semantic_id;
    int16_t score;
    uint16_t frames;
    const char *text;
} chipintelli_asr_result_t;
typedef void (*chipintelli_asr_callback_t)(const chipintelli_asr_result_t *, void *);
void chipintelli_asr_set_callback(chipintelli_asr_callback_t callback, void *arg);
#endif

#ifdef __cplusplus
}
#endif

#define digitalPinToInterrupt(pin) (pin)
#define cli() noInterrupts()
#define sei() interrupts()

#ifdef __cplusplus
long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);
long map(long value, long fromLow, long fromHigh, long toLow, long toHigh);
uint16_t makeWord(uint16_t value);
uint16_t makeWord(uint8_t high, uint8_t low);
#define word(...) makeWord(__VA_ARGS__)
#endif

#include "binary.h"
#include "WCharacter.h"
#include "WString.h"
#include "Printable.h"
#include "Print.h"
#include "Stream.h"
#include "IPAddress.h"
#include "HardwareSerial.h"
#include "pins_arduino.h"

#endif
