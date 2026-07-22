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

// Logical numbering preserves the CI13XX PA/PB/PC port formula. Entries for
// pads that are not bonded out in the SSOP24 package remain reserved.
static constexpr uint8_t NUM_DIGITAL_PINS = 21;
static constexpr uint8_t NUM_ANALOG_INPUTS = 1;
extern const PinDescription g_APinDescription[NUM_DIGITAL_PINS];

// Arduino logical pin aliases for the GPIOs bonded out by the SSOP24 package.
// PA0/PA1 are GPIO only with the internal-RC clock profile.
#define PA0 0U
#define PA1 1U
#define PA2 2U
#define PA3 3U
#define PA4 4U
#define PA5 5U
#define PA6 6U
#define PB5 13U
#define PB6 14U
#define PC4 20U

static constexpr uint8_t A0 = PC4; // PC4 / AIN2

static constexpr uint8_t SDA = PA2; // PA2 / IIC0 SDA
static constexpr uint8_t SCL = PA3; // PA3 / IIC0 SCL
static constexpr uint8_t SDA_MUX = 2; // THIRD_FUNCTION
static constexpr uint8_t SCL_MUX = 2; // THIRD_FUNCTION

// CI1302 has no user-facing hardware SPI controller. These aliases select a
// usable GPIO software-SPI route and intentionally share pads with UART/IIC.
static constexpr uint8_t SCK = PA5;
static constexpr uint8_t MISO = PA2;
static constexpr uint8_t MOSI = PA4; // PG_EN strap during reset
static constexpr uint8_t SS = PA3;

static constexpr uint8_t TX = PB5; // UART0 TX
static constexpr uint8_t RX = PB6; // UART0 RX
static constexpr uint8_t TX1 = PA2; // UART1 TX
static constexpr uint8_t RX1 = PA3; // UART1 RX
static constexpr uint8_t TX2 = PA5; // UART2 TX
static constexpr uint8_t RX2 = PA6; // UART2 RX

static constexpr uint8_t LED_BUILTIN = 255;

#endif
