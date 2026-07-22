#include "Arduino.h"

#define NONE (-1)
#define GPIO_ONLY  (PIN_CAP_GPIO | PIN_CAP_INTERRUPT)
#define GPIO_PWM   (PIN_CAP_GPIO | PIN_CAP_INTERRUPT | PIN_CAP_PWM)
#define GPIO_APWM  (PIN_CAP_GPIO | PIN_CAP_INTERRUPT | PIN_CAP_ADC | PIN_CAP_PWM)

#if USE_EXTERNAL_CRYSTAL_OSC
#define PA0_CAPS 0
#define PA1_CAPS 0
#else
#define PA0_CAPS GPIO_PWM
#define PA1_CAPS GPIO_ONLY
#endif

// CI1302 and CI1303 are pin-compatible SSOP24 parts. Logical pins retain the
// family-wide port numbering: PA0..PA7, PB0..PB7, PC0..PC4. PA0/PA1 become
// GPIO only when the internal-RC clock profile is selected.
const PinDescription g_APinDescription[NUM_DIGITAL_PINS] = {
// port bit pad gpioMux adc  pwm pwmMux caps
    {0, 0,  0, 1, NONE, 5, 2, PA0_CAPS},     //  0 PA0 / XIN
    {0, 1,  1, 1, NONE, NONE, 0, PA1_CAPS},  //  1 PA1 / XOUT
    {0, 2,  6, 0, NONE, 0, 4, GPIO_PWM},     //  2 PA2
    {0, 3,  7, 0, NONE, 1, 4, GPIO_PWM},     //  3 PA3
    {0, 4,  8, 0, NONE, 2, 2, GPIO_PWM},     //  4 PA4 / PG_EN
    {0, 5,  9, 0, NONE, 3, 4, GPIO_PWM},     //  5 PA5
    {0, 6, 10, 0, NONE, 4, 4, GPIO_PWM},     //  6 PA6
    {0, 7, 11, 0, NONE, NONE, 0, 0},         //  7 PA7 unbonded
    {1, 0, 12, 0, NONE, NONE, 0, 0},         //  8 PB0 unbonded
    {1, 1, 13, 0, NONE, NONE, 0, 0},         //  9 PB1 unbonded
    {1, 2, 14, 0, NONE, NONE, 0, 0},         // 10 PB2 unbonded
    {1, 3, 15, 0, NONE, NONE, 0, 0},         // 11 PB3 unbonded
    {1, 4, 16, 0, NONE, NONE, 0, 0},         // 12 PB4 unbonded
    {1, 5, 17, 0, NONE, 1, 3, GPIO_PWM},     // 13 PB5 / UART0 TX
    {1, 6, 18, 0, NONE, 2, 3, GPIO_PWM},     // 14 PB6 / UART0 RX
    {1, 7, 19, 0, NONE, NONE, 0, 0},         // 15 PB7 unbonded
    {2, 0, 20, 0, NONE, NONE, 0, 0},         // 16 PC0 unbonded
    {2, 1, 26, 1, NONE, NONE, 0, 0},         // 17 PC1 unbonded
    {2, 2, 27, 1, NONE, NONE, 0, 0},         // 18 PC2 unbonded
    {2, 3, 28, 1, NONE, NONE, 0, 0},         // 19 PC3 unbonded
    {2, 4, 29, 1, 2,    0, 3, GPIO_APWM}     // 20 PC4 / AIN2
};

#undef NONE
#undef GPIO_ONLY
#undef GPIO_PWM
#undef GPIO_APWM
#undef PA0_CAPS
#undef PA1_CAPS
