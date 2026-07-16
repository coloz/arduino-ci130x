#include "Arduino.h"

extern "C" {
#include "ci130x_adc.h"
#include "ci130x_pwm.h"
#include "ci130x_dpmu.h"
#include "ci130x_core_eclic.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
}

// These two SDK driver functions are implemented in ci130x_adc.c but omitted
// from its public header in V2.7.12.
extern "C" void adc_clear_flag(void);
extern "C" void adc_mask_int(FunctionalState cmd);

// The SDK's default vector is weak and empty even though adc_signal_mode()
// waits for flags set by ADC_irqhandle(). Supply the missing bridge.
extern "C" void __wrap_ADC_IRQHandler(void) {
    ADC_irqhandle();
}

static uint8_t s_readResolution = 12;
static uint8_t s_writeResolution = 8;
static uint32_t s_writeFrequency = 1000;
static bool s_adcReady;
static TimerHandle_t s_toneTimers[6];
static uint8_t s_tonePins[6];
static bool s_toneActive[6];

static pwm_base_t pwmBase(uint8_t channel) {
    static const pwm_base_t bases[] = {PWM0, PWM1, PWM2, PWM3, PWM4, PWM5};
    return bases[channel < 6 ? channel : 0];
}

static uint32_t scaleResolution(uint32_t value, uint8_t from, uint8_t to) {
    if (from > to) return value >> (from - to);
    if (from < to) return value << (to - from);
    return value;
}

static void stopToneTimer(uint8_t channel) {
    if (channel >= 6) return;
    s_toneActive[channel] = false;
    if (s_toneTimers[channel] && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xTimerStop(s_toneTimers[channel], 0);
    }
}

static void stopPwmPin(uint8_t pin) {
    const PinDescription &desc = g_APinDescription[pin];
    pwm_stop(pwmBase(desc.pwmChannel));
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
}

extern "C" void analogReadResolution(uint8_t bits) {
    if (bits >= 1 && bits <= 16) s_readResolution = bits;
}

extern "C" int analogRead(uint8_t pin) {
    if (pin >= NUM_DIGITAL_PINS) return 0;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_ADC)) return 0;
    if (!s_adcReady) {
        scu_set_device_gate(HAL_ADC_BASE, ENABLE);
        adc_poweron();
        adc_reset();
        adc_clear_flag();
        for (uint8_t channel = 0; channel < ADC_CHANNEL_MAX; ++channel) {
            adc_int_clear(static_cast<adc_channelx_t>(channel));
        }
        adc_mask_int(DISABLE);
        eclic_clear_pending(ADC_IRQn);
        eclic_irq_enable(ADC_IRQn);
        s_adcReady = true;
    }
    dpmu_set_io_pull(static_cast<PinPad_Name>(desc.pad), DPMU_IO_PULL_DISABLE);
    dpmu_set_adio_reuse(static_cast<PinPad_Name>(desc.pad), ANALOG_MODE);
    adc_channelx_t channel = static_cast<adc_channelx_t>(desc.adcChannel);
    adc_signal_mode(channel);
    return static_cast<int>(scaleResolution(adc_get_result(channel) & 0x0fffU, 12, s_readResolution));
}

extern "C" void analogWriteResolution(uint8_t bits) {
    if (bits >= 1 && bits <= 16) s_writeResolution = bits;
}

extern "C" void analogWriteFrequency(uint32_t frequency) {
    if (frequency) s_writeFrequency = frequency;
}

extern "C" void analogWrite(uint8_t pin, int value) {
    if (pin >= NUM_DIGITAL_PINS) return;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_PWM)) return;
    stopToneTimer(desc.pwmChannel);
    const uint32_t dutyMax = (1UL << s_writeResolution) - 1UL;
    uint32_t duty = value < 0 ? 0U : static_cast<uint32_t>(value);
    if (duty > dutyMax) duty = dutyMax;

    uint32_t frequency = s_writeFrequency;
    const uint32_t apbClock = get_apb_clk();
    if (frequency > apbClock) frequency = apbClock;
    if (!frequency) return;

    // The V2.7.12 pwm_init() driver multiplies period*duty in 32 bits. Reduce
    // the duty fraction at very low frequencies so the product cannot wrap.
    const uint32_t period = apbClock / frequency;
    uint32_t safeDuty = duty;
    uint32_t safeDutyMax = dutyMax;
    if (period && safeDuty > UINT32_MAX / period) {
        safeDutyMax = UINT32_MAX / period;
        if (!safeDutyMax) safeDutyMax = 1;
        safeDuty = static_cast<uint32_t>(
            (static_cast<uint64_t>(duty) * safeDutyMax + dutyMax / 2U) / dutyMax);
        if (safeDuty > safeDutyMax) safeDuty = safeDutyMax;
    }

    pwm_base_t pwm = pwmBase(desc.pwmChannel);
    scu_set_device_gate(static_cast<uint32_t>(pwm), ENABLE);
    dpmu_set_adio_reuse(static_cast<PinPad_Name>(desc.pad), DIGITAL_MODE);
    dpmu_set_io_reuse(static_cast<PinPad_Name>(desc.pad), static_cast<IOResue_FUNCTION>(desc.pwmMux));
    pwm_init_t config = {0, frequency, safeDuty, safeDutyMax};
    pwm_init(pwm, config);
    pwm_set_restart_md(pwm, 0);
    pwm_start(pwm);
}

static void toneTimerCallback(TimerHandle_t timer) {
    uint8_t channel = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(pvTimerGetTimerID(timer)));
    if (channel < 6 && s_toneTimers[channel] == timer && s_toneActive[channel]) {
        s_toneActive[channel] = false;
        stopPwmPin(s_tonePins[channel]);
    }
}

extern "C" void tone(uint8_t pin, unsigned int frequency, unsigned long duration) {
    if (!frequency || pin >= NUM_DIGITAL_PINS) return;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_PWM)) return;
    uint32_t previous = s_writeFrequency;
    s_writeFrequency = frequency;
    uint8_t previousResolution = s_writeResolution;
    s_writeResolution = 8;
    analogWrite(pin, 128);
    s_writeResolution = previousResolution;
    s_writeFrequency = previous;
    if (duration && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        const uint8_t channel = desc.pwmChannel;
        TickType_t ticks = static_cast<TickType_t>(
            (static_cast<uint64_t>(duration) + portTICK_PERIOD_MS - 1U) / portTICK_PERIOD_MS);
        if (!ticks) ticks = 1;
        if (!s_toneTimers[channel]) {
            s_toneTimers[channel] = xTimerCreate(
                "tone", ticks, pdFALSE,
                reinterpret_cast<void *>(static_cast<uintptr_t>(channel)), toneTimerCallback);
        }
        if (s_toneTimers[channel]) {
            s_tonePins[channel] = pin;
            if (xTimerChangePeriod(s_toneTimers[channel], ticks, 0) == pdPASS) {
                s_toneActive[channel] = true;
            }
        }
    }
}

extern "C" void noTone(uint8_t pin) {
    if (pin >= NUM_DIGITAL_PINS) return;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_PWM)) return;
    stopToneTimer(desc.pwmChannel);
    stopPwmPin(pin);
}
