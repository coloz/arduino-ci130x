#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

enum : uint8_t {
    PIN_CAP_GPIO      = 1U << 0,
    PIN_CAP_INTERRUPT = 1U << 1,
    PIN_CAP_ADC       = 1U << 2,
    PIN_CAP_PWM       = 1U << 3
};

struct PinDescription {
    uint8_t port;
    uint8_t bit;
    uint8_t pad;
    uint8_t gpioMux;
    int8_t adcChannel;
    int8_t pwmChannel;
    uint8_t pwmMux;
    uint8_t capabilities;
};

static constexpr uint8_t NUM_DIGITAL_PINS = 28;
static constexpr uint8_t NUM_ANALOG_INPUTS = 4;
extern const PinDescription g_APinDescription[NUM_DIGITAL_PINS];

static constexpr uint8_t A0 = 20; // PC4 / AIN2
static constexpr uint8_t A1 = 19; // PC3 / AIN3
static constexpr uint8_t A2 = 18; // PC2 / AIN4
static constexpr uint8_t A3 = 17; // PC1 / AIN5

static constexpr uint8_t SDA = 15; // PB7, board-default IIC0 third function
static constexpr uint8_t SCL = 16; // PC0, board-default IIC0 third function

// The CI1306 has no user-facing hardware SPI controller. These aliases are
// the default pins used by the GPIO software SPI library and match the IIS
// header signal group that is free in the current USE_NULL profile.
static constexpr uint8_t SCK = 5;  // PA5 / IIS0_SCLK
static constexpr uint8_t MISO = 2; // PA2 / IIS0_SDI
static constexpr uint8_t MOSI = 4; // PA4 / IIS0_SDO (PG_EN strap during reset)
static constexpr uint8_t SS = 3;   // PA3 / IIS0_LRCLK

static constexpr uint8_t TX = 13;  // PB5, UART0
static constexpr uint8_t RX = 14;  // PB6, UART0
static constexpr uint8_t TX1 = 15; // PB7, UART1
static constexpr uint8_t RX1 = 16; // PC0, UART1
static constexpr uint8_t TX2 = 9;  // PB1, UART2
static constexpr uint8_t RX2 = 10; // PB2, UART2

static constexpr uint8_t LED_BUILTIN = 255;

#endif
