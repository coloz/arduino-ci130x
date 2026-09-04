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

// Logical numbering preserves the CI1306 PA/PB/PC/PD port formula. PA0/PA1
// are occupied by the crystal. PA4 and PC5 are not routed to the 2x13 header;
// PD2 and PD5 are not bonded out by the QFN40 package.
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

static constexpr uint8_t A0 = PC4; // AIN2, shared with AMP MUTE/R11
static constexpr uint8_t A1 = PC3; // AIN3
static constexpr uint8_t A2 = PC2; // AIN4
static constexpr uint8_t A3 = PC1; // AIN5

// The board-populated 4.7 kOhm pull-ups are on IIC0 PB3/PB4.
static constexpr uint8_t SDA = PB3;
static constexpr uint8_t SCL = PB4;
static constexpr uint8_t SDA_MUX = 2; // THIRD_FUNCTION
static constexpr uint8_t SCL_MUX = 2; // THIRD_FUNCTION

// GPIO software-SPI route printed on the board pin map.
static constexpr uint8_t SCK = PD0;
static constexpr uint8_t MISO = PA7;
static constexpr uint8_t MOSI = PD1;
static constexpr uint8_t SS = PB0;
static constexpr uint8_t SS1 = PA2;

static constexpr uint8_t TX = PB5;  // UART0 through onboard USB serial
static constexpr uint8_t RX = PB6;
static constexpr uint8_t TX1 = PB7; // UART1
static constexpr uint8_t RX1 = PC0;
static constexpr uint8_t TX2 = PB1; // UART2
static constexpr uint8_t RX2 = PB2;

// easyVoice 1306 dev onboard/header resources.
static constexpr uint8_t PIN_LED_BUILTIN = PD4;
static constexpr uint8_t LED_BUILTIN = PIN_LED_BUILTIN;
static constexpr uint8_t PIN_POWER_AMPLIFIER_MUTE = PC4;
static constexpr uint8_t PIN_USB_UART_TX = PB5;
static constexpr uint8_t PIN_USB_UART_RX = PB6;
static constexpr uint8_t PIN_SPI_CS0 = SS;
static constexpr uint8_t PIN_SPI_CS1 = SS1;

// PA4, the vendor IR receive default, is not routed to this board's header.
// PA3 is exposed, supports GPIO interrupts, and is the nearest usable route;
// it shares the header pad with IIS0 LRCLK. TX remains on PWM0/PA2.
#define CHIPINTELLI_IR_VARIANT_DEFAULTS 1
static constexpr uint8_t PIN_IR_TX = PA2;
static constexpr uint8_t PIN_IR_RX = PA3;
static constexpr uint8_t PIN_IR_TIMER = 2; // TIMER3 is reserved by SDK BLE

// PC3/PC2 are the adjacent PDM DATA/CLK route on the header. PC1 is the
// alternate PDM DATA pad shown by the pin map.
static constexpr uint8_t PIN_PDM_DATA = PC3;
static constexpr uint8_t PIN_PDM_CLK = PC2;
static constexpr uint8_t PIN_PDM_DATA_ALT = PC1;

static constexpr uint8_t PIN_I2S_MCLK = PA6;
static constexpr uint8_t PIN_I2S_SCLK = PA5;
static constexpr uint8_t PIN_I2S_SDOUT = 255; // PA4 is not on the header
static constexpr uint8_t PIN_I2S_LRCK = PA3;
static constexpr uint8_t PIN_I2S_SDIN = PA2;

#endif
