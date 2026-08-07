#include "Arduino.h"

#define NONE (-1)
#define GPIO       PIN_CAP_GPIO
#define GPIO_IRQ   (PIN_CAP_GPIO | PIN_CAP_INTERRUPT)
#define GPIO_PWM   (PIN_CAP_GPIO | PIN_CAP_INTERRUPT | PIN_CAP_PWM)
#define GPIO_APWM  (PIN_CAP_GPIO | PIN_CAP_INTERRUPT | PIN_CAP_ADC | PIN_CAP_PWM)

// Logical slots preserve the PA/PB/PC/PD port formula. PA0/PA1 are occupied by
// the board's 12.288 MHz crystal; PD2 and PD5 are not bonded out by QFN40.
const PinDescription g_APinDescription[NUM_DIGITAL_PINS] = {
// port bit pad gpioMux adc  pwm pwmMux caps
    {0, 0,  0, 0, NONE, 5, 1, 0},            //  0 PA0 OSC_IN
    {0, 1,  1, 0, NONE, NONE, 0, 0},         //  1 PA1 OSC_OUT
    {0, 2,  6, 0, NONE, 0, 4, GPIO_PWM},     //  2 PA2 / IR TX / IIS0 SDI
    {0, 3,  7, 0, NONE, 1, 4, GPIO_PWM},     //  3 PA3 / IIS0 LRCK
    {0, 4,  8, 0, NONE, 2, 4, GPIO_PWM},     //  4 PA4 / IR RX / IIS0 SDO
    {0, 5,  9, 0, NONE, 3, 4, GPIO_PWM},     //  5 PA5 / IIS0 SCLK
    {0, 6, 10, 0, NONE, 4, 4, GPIO_PWM},     //  6 PA6 / IIS0 MCLK
    {0, 7, 11, 0, NONE, 0, 1, GPIO_PWM},     //  7 PA7 / RGB blue
    {1, 0, 12, 0, NONE, 1, 1, GPIO_PWM},     //  8 PB0 / RGB red
    {1, 1, 13, 0, NONE, 2, 1, GPIO_PWM},     //  9 PB1 / RGB green / UART2 TX
    {1, 2, 14, 0, NONE, 3, 1, GPIO_PWM},     // 10 PB2 / UART2 RX
    {1, 3, 15, 0, NONE, 4, 1, GPIO_PWM},     // 11 PB3
    {1, 4, 16, 0, NONE, 5, 1, GPIO_PWM},     // 12 PB4 / buzzer
    {1, 5, 17, 0, NONE, 1, 3, GPIO_PWM},     // 13 PB5 / UART0 TX
    {1, 6, 18, 0, NONE, 2, 3, GPIO_PWM},     // 14 PB6 / UART0 RX
    {1, 7, 19, 0, NONE, 3, 3, GPIO_PWM},     // 15 PB7 / UART1 TX / IIC SDA
    {2, 0, 20, 0, NONE, 4, 3, GPIO_PWM},     // 16 PC0 / UART1 RX / IIC SCL
    {2, 1, 26, 1, 5, 3, 3, GPIO_APWM},       // 17 PC1 / AIN5
    {2, 2, 27, 1, 4, 2, 3, GPIO_APWM},       // 18 PC2 / AIN4
    {2, 3, 28, 1, 3, 1, 3, GPIO_APWM},       // 19 PC3 / AIN3
    {2, 4, 29, 1, 2, 0, 3, GPIO_APWM},       // 20 PC4 / AIN2 / keys
    {2, 5, 30, 0, NONE, NONE, 0, GPIO_IRQ},  // 21 PC5
    {3, 0, 31, 0, NONE, NONE, 0, GPIO},      // 22 PD0 / amplifier enable
    {3, 1, 32, 0, NONE, NONE, 0, GPIO},      // 23 PD1 / green LED
    {3, 2, 33, 0, NONE, NONE, 0, 0},         // 24 PD2 unbonded
    {3, 3, 34, 0, NONE, NONE, 0, GPIO},      // 25 PD3 / OLED reset
    {3, 4, 35, 0, NONE, NONE, 0, GPIO},      // 26 PD4 / OLED data/control
    {3, 5, 36, 0, NONE, NONE, 0, 0}          // 27 PD5 unbonded
};

#undef NONE
#undef GPIO
#undef GPIO_IRQ
#undef GPIO_PWM
#undef GPIO_APWM
