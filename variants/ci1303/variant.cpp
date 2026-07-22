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

const PinDescription g_APinDescription[NUM_DIGITAL_PINS] = {
// port bit pad gpioMux adc  pwm pwmMux caps
    {0, 0,  0, 1, NONE, 5, 2, PA0_CAPS},
    {0, 1,  1, 1, NONE, NONE, 0, PA1_CAPS},
    {0, 2,  6, 0, NONE, 0, 4, GPIO_PWM},
    {0, 3,  7, 0, NONE, 1, 4, GPIO_PWM},
    {0, 4,  8, 0, NONE, 2, 2, GPIO_PWM},
    {0, 5,  9, 0, NONE, 3, 4, GPIO_PWM},
    {0, 6, 10, 0, NONE, 4, 4, GPIO_PWM},
    {0, 7, 11, 0, NONE, NONE, 0, 0},
    {1, 0, 12, 0, NONE, NONE, 0, 0},
    {1, 1, 13, 0, NONE, NONE, 0, 0},
    {1, 2, 14, 0, NONE, NONE, 0, 0},
    {1, 3, 15, 0, NONE, NONE, 0, 0},
    {1, 4, 16, 0, NONE, NONE, 0, 0},
    {1, 5, 17, 0, NONE, 1, 3, GPIO_PWM},
    {1, 6, 18, 0, NONE, 2, 3, GPIO_PWM},
    {1, 7, 19, 0, NONE, NONE, 0, 0},
    {2, 0, 20, 0, NONE, NONE, 0, 0},
    {2, 1, 26, 1, NONE, NONE, 0, 0},
    {2, 2, 27, 1, NONE, NONE, 0, 0},
    {2, 3, 28, 1, NONE, NONE, 0, 0},
    {2, 4, 29, 1, 2,    0, 3, GPIO_APWM}
};

#undef NONE
#undef GPIO_ONLY
#undef GPIO_PWM
#undef GPIO_APWM
#undef PA0_CAPS
#undef PA1_CAPS
