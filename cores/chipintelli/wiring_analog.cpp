#include "Arduino.h"
#include "PeripheralManager.h"

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
extern "C" void adc_convert_config(adc_channelx_t channel,
                                     adc_clkcyclex_t holdtime);
extern "C" void adc_continuons_convert(FunctionalState cmd);
extern "C" void adc_int_sel(adc_int_mode_t condition);

static TaskHandle_t s_adcWaiter;
static volatile bool s_adcBusy;

// The SDK's default vector is weak and empty even though adc_signal_mode()
// waits for flags set by ADC_irqhandle(). Supply the missing bridge.
extern "C" void __wrap_ADC_IRQHandler(void) {
    ADC_irqhandle();
    TaskHandle_t waiter = s_adcWaiter;
    if (waiter != nullptr) {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(waiter, &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
}

static uint8_t s_readResolution = 12;
static uint8_t s_writeResolution = 8;
static uint32_t s_writeFrequency = 1000;
static bool s_adcReady;
static volatile chipintelli_error_t s_adcLastError = CHIPINTELLI_ERROR_NONE;
constexpr uint32_t kAdcHardwareTimeoutUs = 5000U;
constexpr uint32_t kAdcConversionTimeoutMs = 10U;
static TimerHandle_t s_toneTimers[6];
static uint8_t s_tonePins[6];
static bool s_toneActive[6];

static void releaseAdcRead(uint8_t pin) {
    (void)pinModeOwned(pin, INPUT, PeripheralOwner::Adc);
    PeripheralManager.releasePin(PeripheralOwner::Adc, pin);
    taskENTER_CRITICAL();
    s_adcWaiter = nullptr;
    s_adcBusy = false;
    taskEXIT_CRITICAL();
}
static uint8_t s_pwmPins[6] = {255, 255, 255, 255, 255, 255};
static PeripheralOwner s_pwmOwners[6] = {};

static pwm_base_t pwmBase(uint8_t channel) {
    static const pwm_base_t bases[] = {PWM0, PWM1, PWM2, PWM3, PWM4, PWM5};
    return bases[channel < 6 ? channel : 0];
}

static PeripheralResource pwmResource(uint8_t channel) {
    return static_cast<PeripheralResource>(
        static_cast<uint8_t>(PeripheralResource::Pwm0) + channel);
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

static void releasePwmPin(uint8_t pin, PeripheralOwner owner) {
    const PinDescription &desc = g_APinDescription[pin];
    pwm_stop(pwmBase(desc.pwmChannel));
    (void)pinModeOwned(pin, OUTPUT, owner);
    digitalWrite(pin, LOW);
    const PeripheralResource resource = pwmResource(desc.pwmChannel);
    PeripheralManager.release(owner, &pin, 1, &resource, 1);
    s_pwmPins[desc.pwmChannel] = 255;
    s_pwmOwners[desc.pwmChannel] = PeripheralOwner::None;
}

static bool claimPwmPin(uint8_t pin, PeripheralOwner owner) {
    const uint8_t channel = g_APinDescription[pin].pwmChannel;
    if (s_pwmOwners[channel] == owner && s_pwmPins[channel] != pin &&
        s_pwmPins[channel] < NUM_DIGITAL_PINS) {
        releasePwmPin(s_pwmPins[channel], owner);
    }
    const PeripheralResource resource = pwmResource(channel);
    if (!PeripheralManager.claim(owner, &pin, 1, &resource, 1)) return false;
    s_pwmPins[channel] = pin;
    s_pwmOwners[channel] = owner;
    return true;
}

extern "C" void analogReadResolution(uint8_t bits) {
    if (bits >= 1 && bits <= 16) s_readResolution = bits;
}

extern "C" int analogRead(uint8_t pin) {
    if (pin >= NUM_DIGITAL_PINS) {
        s_adcLastError = CHIPINTELLI_ERROR_HARDWARE_FAULT;
        return 0;
    }
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_ADC)) {
        s_adcLastError = CHIPINTELLI_ERROR_HARDWARE_FAULT;
        return 0;
    }
    taskENTER_CRITICAL();
    if (s_adcBusy) {
        taskEXIT_CRITICAL();
        s_adcLastError = CHIPINTELLI_ERROR_BUSY;
        return 0;
    }
    s_adcBusy = true;
    taskEXIT_CRITICAL();
    if (!PeripheralManager.claimPin(PeripheralOwner::Adc, pin)) {
        taskENTER_CRITICAL();
        s_adcBusy = false;
        taskEXIT_CRITICAL();
        s_adcLastError = CHIPINTELLI_ERROR_BUSY;
        return 0;
    }
    detachInterrupt(pin);
    if (!s_adcReady) {
        scu_set_device_gate(HAL_ADC_BASE, ENABLE);
        if (adc_poweron_timeout(kAdcHardwareTimeoutUs) != RETURN_OK ||
            adc_reset_timeout(kAdcHardwareTimeoutUs) != RETURN_OK) {
            s_adcLastError = CHIPINTELLI_ERROR_TIMEOUT;
            releaseAdcRead(pin);
            return 0;
        }
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
    bool converted = false;
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        TaskHandle_t current = xTaskGetCurrentTaskHandle();
        taskENTER_CRITICAL();
        s_adcWaiter = current;
        taskEXIT_CRITICAL();
        (void)ulTaskNotifyTake(pdTRUE, 0U);
        adc_clear_flag();
        adc_int_clear(channel);
        adc_convert_config(channel, ADC_CLKCYCLE_2);
        adc_continuons_convert(DISABLE);
        adc_int_sel(ADC_INT_MODE_TRANS_END);
        adc_soc_soft_ctrl(ENABLE);
        const uint32_t conversionStarted = millis();
        for (;;) {
            // Direct task notifications are also used to wake the Arduino task
            // for UART/GPIO/Timer events. Confirm the ADC channel flag instead
            // of treating an unrelated notification as conversion completion.
            if (adc_wait_int_timeout(channel, 1U) == RETURN_OK) {
                converted = true;
                break;
            }
            const uint32_t elapsed = millis() - conversionStarted;
            if (elapsed >= kAdcConversionTimeoutMs) break;
            const uint32_t remaining = kAdcConversionTimeoutMs - elapsed;
            TickType_t wait = static_cast<TickType_t>(
                (remaining + portTICK_PERIOD_MS - 1U) /
                portTICK_PERIOD_MS);
            (void)ulTaskNotifyTake(pdTRUE, wait > 0U ? wait : 1U);
        }
    } else {
        adc_convert_config(channel, ADC_CLKCYCLE_2);
        adc_continuons_convert(DISABLE);
        adc_int_sel(ADC_INT_MODE_TRANS_END);
        adc_soc_soft_ctrl(ENABLE);
        converted = adc_wait_int_timeout(channel, kAdcHardwareTimeoutUs) ==
                    RETURN_OK;
    }
    if (!converted) {
        s_adcLastError = CHIPINTELLI_ERROR_TIMEOUT;
        adc_int_clear(channel);
        releaseAdcRead(pin);
        return 0;
    }
    const int result = static_cast<int>(
        scaleResolution(adc_get_result(channel) & 0x0fffU, 12,
                        s_readResolution));
    releaseAdcRead(pin);
    s_adcLastError = CHIPINTELLI_ERROR_NONE;
    return result;
}

extern "C" chipintelli_error_t analogReadLastError(void) {
    return s_adcLastError;
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
    if (s_pwmOwners[desc.pwmChannel] == PeripheralOwner::Tone &&
        s_pwmPins[desc.pwmChannel] < NUM_DIGITAL_PINS) {
        releasePwmPin(s_pwmPins[desc.pwmChannel], PeripheralOwner::Tone);
    }
    if (!claimPwmPin(pin, PeripheralOwner::AnalogWrite)) return;
    detachInterrupt(pin);
    const uint32_t dutyMax = (1UL << s_writeResolution) - 1UL;
    uint32_t duty = value < 0 ? 0U : static_cast<uint32_t>(value);
    if (duty > dutyMax) duty = dutyMax;

    // CI130X PWM hardware cannot generate a true 100% duty cycle. Handle
    // both constant-output endpoints as GPIO levels and reserve PWM for the
    // open interval (0, dutyMax).
    pwm_base_t pwm = pwmBase(desc.pwmChannel);
    if (duty == 0U || duty == dutyMax) {
        scu_set_device_gate(static_cast<uint32_t>(pwm), ENABLE);
        pwm_stop(pwm);
        (void)pinModeOwned(pin, OUTPUT, PeripheralOwner::AnalogWrite);
        digitalWrite(pin, duty == dutyMax ? HIGH : LOW);
        const PeripheralResource resource = pwmResource(desc.pwmChannel);
        PeripheralManager.release(PeripheralOwner::AnalogWrite, &pin, 1,
                                  &resource, 1);
        s_pwmPins[desc.pwmChannel] = 255;
        s_pwmOwners[desc.pwmChannel] = PeripheralOwner::None;
        return;
    }

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
        releasePwmPin(s_tonePins[channel], PeripheralOwner::Tone);
    }
}

extern "C" void tone(uint8_t pin, unsigned int frequency, unsigned long duration) {
    if (!frequency || pin >= NUM_DIGITAL_PINS) return;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_PWM)) return;
    stopToneTimer(desc.pwmChannel);
    if (s_pwmOwners[desc.pwmChannel] == PeripheralOwner::AnalogWrite &&
        s_pwmPins[desc.pwmChannel] < NUM_DIGITAL_PINS) {
        releasePwmPin(s_pwmPins[desc.pwmChannel],
                      PeripheralOwner::AnalogWrite);
    }
    if (!claimPwmPin(pin, PeripheralOwner::Tone)) return;
    detachInterrupt(pin);

    const pwm_base_t pwm = pwmBase(desc.pwmChannel);
    scu_set_device_gate(static_cast<uint32_t>(pwm), ENABLE);
    dpmu_set_adio_reuse(static_cast<PinPad_Name>(desc.pad), DIGITAL_MODE);
    dpmu_set_io_reuse(static_cast<PinPad_Name>(desc.pad),
                      static_cast<IOResue_FUNCTION>(desc.pwmMux));
    pwm_init_t config = {0, frequency, 128, 255};
    pwm_init(pwm, config);
    pwm_set_restart_md(pwm, 0);
    pwm_start(pwm);
    if (!duration) return;

    const uint8_t channel = desc.pwmChannel;
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        releasePwmPin(pin, PeripheralOwner::Tone);
        return;
    }

    TickType_t ticks = static_cast<TickType_t>(
        (static_cast<uint64_t>(duration) + portTICK_PERIOD_MS - 1U) /
        portTICK_PERIOD_MS);
    if (!ticks) ticks = 1;
    if (!s_toneTimers[channel]) {
        s_toneTimers[channel] = xTimerCreate(
            "tone", ticks, pdFALSE,
            reinterpret_cast<void *>(static_cast<uintptr_t>(channel)),
            toneTimerCallback);
    }
    if (s_toneTimers[channel]) {
        s_tonePins[channel] = pin;
        if (xTimerChangePeriod(s_toneTimers[channel], ticks, 0) == pdPASS) {
            s_toneActive[channel] = true;
            return;
        }
    }

    // A finite-duration tone must fail closed. Otherwise heap exhaustion or a
    // full timer command queue would leave the PWM running indefinitely.
    releasePwmPin(pin, PeripheralOwner::Tone);
}

extern "C" void noTone(uint8_t pin) {
    if (pin >= NUM_DIGITAL_PINS) return;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_PWM)) return;
    stopToneTimer(desc.pwmChannel);
    if (s_pwmOwners[desc.pwmChannel] == PeripheralOwner::Tone &&
        s_pwmPins[desc.pwmChannel] == pin) {
        releasePwmPin(pin, PeripheralOwner::Tone);
    }
}
