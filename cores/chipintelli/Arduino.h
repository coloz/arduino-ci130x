#ifndef Arduino_h
#define Arduino_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pgmspace.h"
#include "ArduinoEvent.h"

// Keep every Arduino and packaged SDK translation unit on the same board and
// algorithm profile. The platform adds the SDK include directory.
#include "user_config.h"

#ifdef __cplusplus
#include <cmath>
#include <algorithm>
#endif

#define ARDUINO_CORE_VERSION_MAJOR 0
#define ARDUINO_CORE_VERSION_MINOR 0
#define ARDUINO_CORE_VERSION_PATCH 1

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

#define SERIAL_PARITY_EVEN   (0x1ul)
#define SERIAL_PARITY_ODD    (0x2ul)
#define SERIAL_PARITY_NONE   (0x3ul)
#define SERIAL_PARITY_MARK   (0x4ul)
#define SERIAL_PARITY_SPACE  (0x5ul)
#define SERIAL_PARITY_MASK   (0xFul)

#define SERIAL_STOP_BIT_1    (0x10ul)
#define SERIAL_STOP_BIT_1_5  (0x20ul)
#define SERIAL_STOP_BIT_2    (0x30ul)
#define SERIAL_STOP_BIT_MASK (0xF0ul)

#define SERIAL_DATA_5        (0x100ul)
#define SERIAL_DATA_6        (0x200ul)
#define SERIAL_DATA_7        (0x300ul)
#define SERIAL_DATA_8        (0x400ul)
#define SERIAL_DATA_MASK     (0xF00ul)

#define SERIAL_5N1 (SERIAL_STOP_BIT_1 | SERIAL_PARITY_NONE | SERIAL_DATA_5)
#define SERIAL_6N1 (SERIAL_STOP_BIT_1 | SERIAL_PARITY_NONE | SERIAL_DATA_6)
#define SERIAL_7N1 (SERIAL_STOP_BIT_1 | SERIAL_PARITY_NONE | SERIAL_DATA_7)
#define SERIAL_8N1 (SERIAL_STOP_BIT_1 | SERIAL_PARITY_NONE | SERIAL_DATA_8)
#define SERIAL_5N2 (SERIAL_STOP_BIT_2 | SERIAL_PARITY_NONE | SERIAL_DATA_5)
#define SERIAL_6N2 (SERIAL_STOP_BIT_2 | SERIAL_PARITY_NONE | SERIAL_DATA_6)
#define SERIAL_7N2 (SERIAL_STOP_BIT_2 | SERIAL_PARITY_NONE | SERIAL_DATA_7)
#define SERIAL_8N2 (SERIAL_STOP_BIT_2 | SERIAL_PARITY_NONE | SERIAL_DATA_8)
#define SERIAL_5E1 (SERIAL_STOP_BIT_1 | SERIAL_PARITY_EVEN | SERIAL_DATA_5)
#define SERIAL_6E1 (SERIAL_STOP_BIT_1 | SERIAL_PARITY_EVEN | SERIAL_DATA_6)
#define SERIAL_7E1 (SERIAL_STOP_BIT_1 | SERIAL_PARITY_EVEN | SERIAL_DATA_7)
#define SERIAL_8E1 (SERIAL_STOP_BIT_1 | SERIAL_PARITY_EVEN | SERIAL_DATA_8)
#define SERIAL_5E2 (SERIAL_STOP_BIT_2 | SERIAL_PARITY_EVEN | SERIAL_DATA_5)
#define SERIAL_6E2 (SERIAL_STOP_BIT_2 | SERIAL_PARITY_EVEN | SERIAL_DATA_6)
#define SERIAL_7E2 (SERIAL_STOP_BIT_2 | SERIAL_PARITY_EVEN | SERIAL_DATA_7)
#define SERIAL_8E2 (SERIAL_STOP_BIT_2 | SERIAL_PARITY_EVEN | SERIAL_DATA_8)
#define SERIAL_5O1 (SERIAL_STOP_BIT_1 | SERIAL_PARITY_ODD | SERIAL_DATA_5)
#define SERIAL_6O1 (SERIAL_STOP_BIT_1 | SERIAL_PARITY_ODD | SERIAL_DATA_6)
#define SERIAL_7O1 (SERIAL_STOP_BIT_1 | SERIAL_PARITY_ODD | SERIAL_DATA_7)
#define SERIAL_8O1 (SERIAL_STOP_BIT_1 | SERIAL_PARITY_ODD | SERIAL_DATA_8)
#define SERIAL_5O2 (SERIAL_STOP_BIT_2 | SERIAL_PARITY_ODD | SERIAL_DATA_5)
#define SERIAL_6O2 (SERIAL_STOP_BIT_2 | SERIAL_PARITY_ODD | SERIAL_DATA_6)
#define SERIAL_7O2 (SERIAL_STOP_BIT_2 | SERIAL_PARITY_ODD | SERIAL_DATA_7)
#define SERIAL_8O2 (SERIAL_STOP_BIT_2 | SERIAL_PARITY_ODD | SERIAL_DATA_8)

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
void attachInterruptISR(uint8_t pin, voidFuncPtr callback, int mode);
void attachInterruptArgISR(uint8_t pin, voidFuncPtrArg callback, void *arg,
                           int mode);
void detachInterrupt(uint8_t pin);

typedef uint8_t chipintelli_loop_mode_t;
enum {
    CHIPINTELLI_LOOP_COMPATIBLE = 0,
    CHIPINTELLI_LOOP_EVENT_DRIVEN,
    CHIPINTELLI_LOOP_LOW_POWER
};

typedef uint8_t chipintelli_arduino_fault_t;
enum {
    CHIPINTELLI_ARDUINO_FAULT_NONE = 0,
    CHIPINTELLI_ARDUINO_FAULT_NO_MEMORY,
    CHIPINTELLI_ARDUINO_FAULT_HARDWARE
};

bool chipintelli_arduino_set_loop_mode(chipintelli_loop_mode_t mode);
chipintelli_loop_mode_t chipintelli_arduino_loop_mode(void);
bool chipintelli_arduino_set_interactive(bool enabled);
chipintelli_arduino_fault_t chipintelli_arduino_fault(void);

typedef uint8_t chipintelli_error_t;
enum {
    CHIPINTELLI_ERROR_NONE = 0,
    CHIPINTELLI_ERROR_TIMEOUT,
    CHIPINTELLI_ERROR_BUSY,
    CHIPINTELLI_ERROR_NO_MEMORY,
    CHIPINTELLI_ERROR_HARDWARE_FAULT,
    CHIPINTELLI_ERROR_QUEUE_FULL
};
chipintelli_error_t analogReadLastError(void);

enum {
    CHIPINTELLI_LIVENESS_ARDUINO_LOOP = 1UL << 0,
    CHIPINTELLI_LIVENESS_SDK_AUDIO = 1UL << 1,
    CHIPINTELLI_LIVENESS_APPLICATION = 1UL << 2,
    CHIPINTELLI_LIVENESS_USER0 = 1UL << 8,
    CHIPINTELLI_LIVENESS_USER1 = 1UL << 9,
    CHIPINTELLI_LIVENESS_USER2 = 1UL << 10,
    CHIPINTELLI_LIVENESS_USER3 = 1UL << 11
};
void chipintelli_watchdog_set_liveness_mask(uint32_t mask);
uint32_t chipintelli_watchdog_liveness_mask(void);
void chipintelli_watchdog_heartbeat(uint32_t sources);

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
    bool is_wake_word;
    const char *text;
} chipintelli_asr_result_t;
typedef void (*chipintelli_asr_callback_t)(const chipintelli_asr_result_t *, void *);

typedef uint8_t chipintelli_asr_event_type_t;
enum {
    CHIPINTELLI_ASR_EVENT_STARTED = 1,
    CHIPINTELLI_ASR_EVENT_WAKEUP,
    CHIPINTELLI_ASR_EVENT_TIMEOUT
};
typedef void (*chipintelli_asr_event_callback_t)(chipintelli_asr_event_type_t,
                                                  void *);

typedef uint8_t chipintelli_sdk_state_t;
enum {
    CHIPINTELLI_SDK_NOT_STARTED = 0,
    CHIPINTELLI_SDK_STARTING,
    CHIPINTELLI_SDK_READY,
    CHIPINTELLI_SDK_FAILED
};

bool chipintelli_sdk_begin(void);
chipintelli_sdk_state_t chipintelli_sdk_state(void);
void chipintelli_asr_set_callback(chipintelli_asr_callback_t callback, void *arg);
void chipintelli_asr_set_event_callback(chipintelli_asr_event_callback_t callback,
                                        void *arg);
bool chipintelli_asr_is_awake(void);
bool chipintelli_asr_keep_awake_for(uint32_t timeout_ms);
bool chipintelli_asr_set_wake_word_enabled(bool enabled);
#endif

#ifndef CHIPINTELLI_CWSL_CORE_HOOK_TYPES
#define CHIPINTELLI_CWSL_CORE_HOOK_TYPES
typedef uint8_t chipintelli_cwsl_word_type_t;
enum {
    CHIPINTELLI_CWSL_COMMAND_WORD = 0,
    CHIPINTELLI_CWSL_WAKE_WORD,
    CHIPINTELLI_CWSL_ALL_WORDS
};

typedef uint8_t chipintelli_cwsl_state_t;
enum {
    CHIPINTELLI_CWSL_IDLE = 0,
    CHIPINTELLI_CWSL_RECOGNIZING,
    CHIPINTELLI_CWSL_LEARNING,
    CHIPINTELLI_CWSL_DELETING,
    CHIPINTELLI_CWSL_UNAVAILABLE = 0xFF
};

typedef uint8_t chipintelli_cwsl_event_type_t;
enum {
    CHIPINTELLI_CWSL_LEARNING_STARTED = 1,
    CHIPINTELLI_CWSL_RECORDING_STARTED,
    CHIPINTELLI_CWSL_ATTEMPT_RESULT,
    CHIPINTELLI_CWSL_LEARNING_SUCCEEDED,
    CHIPINTELLI_CWSL_LEARNING_FAILED,
    CHIPINTELLI_CWSL_LEARNING_CANCELLED,
    CHIPINTELLI_CWSL_DELETE_SUCCEEDED,
    CHIPINTELLI_CWSL_RECOGNIZED,
    CHIPINTELLI_CWSL_DELETE_FAILED
};

typedef struct chipintelli_cwsl_event_t {
    chipintelli_cwsl_event_type_t type;
    chipintelli_cwsl_word_type_t word_type;
    uint8_t attempt;
    uint8_t result;
    uint32_t command_id;
    uint16_t group_id;
    uint32_t distance;
} chipintelli_cwsl_event_t;
typedef void (*chipintelli_cwsl_callback_t)(const chipintelli_cwsl_event_t *, void *);

bool chipintelli_cwsl_profile_enabled(void);
void chipintelli_cwsl_set_callback(chipintelli_cwsl_callback_t callback, void *arg);
bool chipintelli_cwsl_learn(uint32_t command_id,
                            uint16_t group_id,
                            chipintelli_cwsl_word_type_t word_type);
bool chipintelli_cwsl_cancel(void);
bool chipintelli_cwsl_erase(uint32_t command_id,
                            uint16_t group_id,
                            chipintelli_cwsl_word_type_t word_type);
chipintelli_cwsl_state_t chipintelli_cwsl_state(void);
int chipintelli_cwsl_template_count(chipintelli_cwsl_word_type_t word_type);
int chipintelli_cwsl_remaining_templates(void);
int chipintelli_cwsl_max_templates(void);
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
