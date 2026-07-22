#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include "../ci130x_pin_aliases.h"

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

// Arduino logical pin aliases for GPIOs bonded out by the QFN40 package.
#define PA0 0U
#define PA1 1U
#define PA2 2U
#define PA3 3U
#define PA4 4U
#define PA5 5U
#define PA6 6U
#define PA7 7U
#define PB0 8U
#define PB1 9U
#define PB2 10U
#define PB3 11U
#define PB4 12U
#define PB5 13U
#define PB6 14U
#define PB7 15U
#define PC0 16U
#define PC1 17U
#define PC2 18U
#define PC3 19U
#define PC4 20U
#define PC5 21U
#define PD0 22U
#define PD1 23U
#define PD3 25U
#define PD4 26U

static constexpr uint8_t A0 = PC4; // AIN2
static constexpr uint8_t A1 = PC3; // AIN3
static constexpr uint8_t A2 = PC2; // AIN4
static constexpr uint8_t A3 = PC1; // AIN5

static constexpr uint8_t SDA = PB7; // board-default IIC0 third function
static constexpr uint8_t SCL = PC0; // board-default IIC0 third function
static constexpr uint8_t SDA_MUX = 2; // THIRD_FUNCTION
static constexpr uint8_t SCL_MUX = 2; // THIRD_FUNCTION

// The CI1306 has no user-facing hardware SPI controller. These aliases are
// the default pins used by the GPIO software SPI library and match the IIS
// header signal group that is free in the current USE_NULL profile.
static constexpr uint8_t SCK = PA5;  // IIS0_SCLK
static constexpr uint8_t MISO = PA2; // IIS0_SDI
static constexpr uint8_t MOSI = PA4; // IIS0_SDO (PG_EN strap during reset)
static constexpr uint8_t SS = PA3;   // IIS0_LRCLK

static constexpr uint8_t TX = PB5;  // UART0
static constexpr uint8_t RX = PB6;  // UART0
static constexpr uint8_t TX1 = PB7; // UART1
static constexpr uint8_t RX1 = PC0; // UART1
static constexpr uint8_t TX2 = PB1; // UART2
static constexpr uint8_t RX2 = PB2; // UART2

static constexpr uint8_t LED_BUILTIN = 255;

#endif
