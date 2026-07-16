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

// Logical numbering preserves the CI13XX PA/PB/PC port formula. Entries for
// pads that are not bonded out in the SSOP24 package remain reserved.
static constexpr uint8_t NUM_DIGITAL_PINS = 21;
static constexpr uint8_t NUM_ANALOG_INPUTS = 1;
extern const PinDescription g_APinDescription[NUM_DIGITAL_PINS];

static constexpr uint8_t A0 = 20; // PC4 / AIN2

static constexpr uint8_t SDA = 2; // PA2 / IIC0 SDA
static constexpr uint8_t SCL = 3; // PA3 / IIC0 SCL
static constexpr uint8_t SDA_MUX = 2; // THIRD_FUNCTION
static constexpr uint8_t SCL_MUX = 2; // THIRD_FUNCTION

// CI1302 has no user-facing hardware SPI controller. These aliases select a
// usable GPIO software-SPI route and intentionally share pads with UART/IIC.
static constexpr uint8_t SCK = 5;  // PA5
static constexpr uint8_t MISO = 2; // PA2
static constexpr uint8_t MOSI = 4; // PA4 / PG_EN strap during reset
static constexpr uint8_t SS = 3;   // PA3

static constexpr uint8_t TX = 13; // PB5 / UART0 TX
static constexpr uint8_t RX = 14; // PB6 / UART0 RX
static constexpr uint8_t TX1 = 2; // PA2 / UART1 TX
static constexpr uint8_t RX1 = 3; // PA3 / UART1 RX
static constexpr uint8_t TX2 = 5; // PA5 / UART2 TX
static constexpr uint8_t RX2 = 6; // PA6 / UART2 RX

static constexpr uint8_t LED_BUILTIN = 255;

#endif
