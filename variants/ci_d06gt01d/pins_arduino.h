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

// Arduino logical pin aliases for GPIOs bonded out by the CI1306 QFN40.
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

static constexpr uint8_t A0 = PC4; // AIN2, five-key resistor ladder
static constexpr uint8_t A1 = PC3; // AIN3
static constexpr uint8_t A2 = PC2; // AIN4
static constexpr uint8_t A3 = PC1; // AIN5

// The IIC/UART1 header and the OLED connector share this bus.
static constexpr uint8_t SDA = PB7;
static constexpr uint8_t SCL = PC0;
static constexpr uint8_t SDA_MUX = 2; // THIRD_FUNCTION
static constexpr uint8_t SCL_MUX = 2; // THIRD_FUNCTION

// GPIO software-SPI defaults on the board's IIS header.
static constexpr uint8_t SCK = PA5;  // IIS0 SCLK
static constexpr uint8_t MISO = PA2; // IIS0 SDI
static constexpr uint8_t MOSI = PA4; // IIS0 SDO (PG_EN strap during reset)
static constexpr uint8_t SS = PA3;   // IIS0 LRCK

static constexpr uint8_t TX = PB5;  // UART0 through the onboard USB bridge
static constexpr uint8_t RX = PB6;
static constexpr uint8_t TX1 = PB7; // UART1/IIC header
static constexpr uint8_t RX1 = PC0;
static constexpr uint8_t TX2 = PB1;
static constexpr uint8_t RX2 = PB2;

// CI-D06GT01D onboard peripherals. PIN_OLED_CS is intentionally NC in the
// V1.0 schematic, so code using the IIC OLED connector should not drive it.
static constexpr uint8_t PIN_LED_BUILTIN = PD1; // active HIGH
static constexpr uint8_t LED_BUILTIN = PIN_LED_BUILTIN;
static constexpr uint8_t PIN_RGB_LED_RED = PB0;   // PWM1
static constexpr uint8_t PIN_RGB_LED_GREEN = PB1; // PWM2
static constexpr uint8_t PIN_RGB_LED_BLUE = PA7;  // PWM0
static constexpr uint8_t PIN_KEY_ADC = A0;
static constexpr uint8_t PIN_BUZZER = PB4; // PWM5
static constexpr uint8_t PIN_IR_TX = PA2;  // PWM0
static constexpr uint8_t PIN_IR_RX = PA4;
static constexpr uint8_t PIN_POWER_AMPLIFIER_ENABLE = PD0;
static constexpr uint8_t PIN_OLED_RESET = PD3;
static constexpr uint8_t PIN_OLED_DC = PD4;
static constexpr uint8_t PIN_OLED_CS = 255;
static constexpr uint8_t PIN_PDM_CLK = PC0;
static constexpr uint8_t PIN_PDM_DATA = PB7;
static constexpr uint8_t PIN_I2S_MCLK = PA6;
static constexpr uint8_t PIN_I2S_SCLK = PA5;
static constexpr uint8_t PIN_I2S_SDOUT = PA4;
static constexpr uint8_t PIN_I2S_LRCK = PA3;
static constexpr uint8_t PIN_I2S_SDIN = PA2;

#endif
