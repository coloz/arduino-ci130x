#include "Arduino.h"
#include "ArduinoEvent.h"
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
extern "C" void adc_powerdown(void);

static uint8_t s_readResolution = 12;
static uint8_t s_writeResolution = 8;
static uint32_t s_writeFrequency = 1000;
static volatile bool s_adcReady;
static volatile chipintelli_error_t s_adcLastError = CHIPINTELLI_ERROR_NONE;
constexpr uint32_t kAdcHardwareTimeoutUs = 5000U;
constexpr uint32_t kAdcConversionTimeoutMs = 10U;
constexpr uint32_t kAdcIdlePowerDownMs = 20U;
constexpr uint8_t kNoAdcPin = 0xffU;

enum class AdcOperation : uint8_t { Idle, Synchronous, Asynchronous, Continuous };

static volatile AdcOperation s_adcOperation = AdcOperation::Idle;
static volatile bool s_adcBusy;
static volatile bool s_adcConversionActive;
static volatile bool s_adcConversionDone;
static volatile bool s_adcEventPending;
static volatile uint16_t s_adcRawValue;
static volatile chipintelli_error_t s_adcCompletionError =
    CHIPINTELLI_ERROR_NONE;
static volatile uint32_t s_adcGeneration;
static volatile uint32_t s_adcDropped;
static volatile uint8_t s_adcPin = kNoAdcPin;
static volatile uint8_t s_adcReleasePendingPin = kNoAdcPin;
static adc_channelx_t s_adcChannel = ADC_CHANNEL_0;
static TaskHandle_t s_adcWaiter;
static analog_read_callback_t s_adcCallback;
static void *s_adcCallbackContext;
static TimerHandle_t s_adcIdleTimer;
static TimerHandle_t s_adcContinuousTimer;
static TimerHandle_t s_adcTimeoutTimer;

static void finishAdcOperation(uint8_t pin);
static void adcCompletionEvent(void *context, uint32_t generation);

static TimerHandle_t s_toneTimers[6];
static uint8_t s_tonePins[6];
static bool s_toneActive[6];
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

static void releasePendingAdcPin() {
    uint8_t pin = kNoAdcPin;
    taskENTER_CRITICAL();
    pin = s_adcReleasePendingPin;
    s_adcReleasePendingPin = kNoAdcPin;
    taskEXIT_CRITICAL();
    if (pin < NUM_DIGITAL_PINS) {
        (void)pinModeOwned(pin, INPUT, PeripheralOwner::Adc);
        PeripheralManager.releasePin(PeripheralOwner::Adc, pin);
    }
}

static void adcPowerDownNow() {
    if (!s_adcReady) return;
    adc_mask_int(ENABLE);
    eclic_irq_disable(ADC_IRQn);
    adc_powerdown();
    scu_set_device_gate(HAL_ADC_BASE, DISABLE);
    s_adcReady = false;
}

static void adcIdleTimerCallback(TimerHandle_t) {
    releasePendingAdcPin();
    taskENTER_CRITICAL();
    const bool idle = !s_adcBusy && !s_adcConversionActive;
    taskEXIT_CRITICAL();
    if (idle) adcPowerDownNow();
}

static void adcTimeoutTimerCallback(TimerHandle_t) {
    uint8_t pin = kNoAdcPin;
    AdcOperation operation = AdcOperation::Idle;
    taskENTER_CRITICAL();
    if (s_adcConversionActive) {
        s_adcConversionActive = false;
        s_adcConversionDone = false;
        operation = s_adcOperation;
        pin = s_adcPin;
    }
    taskEXIT_CRITICAL();
    if (operation == AdcOperation::Idle) return;
    adc_int_clear(s_adcChannel);
    s_adcLastError = CHIPINTELLI_ERROR_TIMEOUT;
    if (operation == AdcOperation::Synchronous) {
        TaskHandle_t waiter = s_adcWaiter;
        if (waiter != nullptr) xTaskNotifyGive(waiter);
    } else {
        s_adcCompletionError = CHIPINTELLI_ERROR_TIMEOUT;
        s_adcEventPending = true;
        if (!chipintelli_arduino_post_event(adcCompletionEvent, nullptr,
                                             s_adcGeneration)) {
            s_adcEventPending = false;
            ++s_adcDropped;
            s_adcLastError = CHIPINTELLI_ERROR_QUEUE_FULL;
            if (operation == AdcOperation::Asynchronous &&
                pin < NUM_DIGITAL_PINS) {
                finishAdcOperation(pin);
            }
        }
    }
}

static void scheduleAdcPowerDown() {
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        adcPowerDownNow();
        return;
    }
    if (s_adcIdleTimer == nullptr) {
        s_adcIdleTimer = xTimerCreate(
            "adc-idle", pdMS_TO_TICKS(kAdcIdlePowerDownMs), pdFALSE, nullptr,
            adcIdleTimerCallback);
    }
    if (s_adcIdleTimer == nullptr ||
        xTimerChangePeriod(s_adcIdleTimer,
                           pdMS_TO_TICKS(kAdcIdlePowerDownMs), 0U) != pdPASS) {
        adcPowerDownNow();
    }
}

static bool ensureAdcPower() {
    if (s_adcReady) return true;
    scu_set_device_gate(HAL_ADC_BASE, ENABLE);
    if (adc_poweron_timeout(kAdcHardwareTimeoutUs) != RETURN_OK ||
        adc_reset_timeout(kAdcHardwareTimeoutUs) != RETURN_OK) {
        adc_powerdown();
        scu_set_device_gate(HAL_ADC_BASE, DISABLE);
        s_adcLastError = CHIPINTELLI_ERROR_TIMEOUT;
        return false;
    }
    adc_clear_flag();
    for (uint8_t channel = 0; channel < ADC_CHANNEL_MAX; ++channel) {
        adc_int_clear(static_cast<adc_channelx_t>(channel));
    }
    adc_mask_int(DISABLE);
    eclic_clear_pending(ADC_IRQn);
    eclic_irq_enable(ADC_IRQn);
    s_adcReady = true;
    return true;
}

static bool prepareAdcPin(uint8_t pin) {
    releasePendingAdcPin();
    if (pin >= NUM_DIGITAL_PINS ||
        (g_APinDescription[pin].capabilities & PIN_CAP_ADC) == 0U) {
        s_adcLastError = CHIPINTELLI_ERROR_HARDWARE_FAULT;
        return false;
    }
    taskENTER_CRITICAL();
    if (s_adcBusy) {
        taskEXIT_CRITICAL();
        s_adcLastError = CHIPINTELLI_ERROR_BUSY;
        return false;
    }
    s_adcBusy = true;
    taskEXIT_CRITICAL();
    if (!PeripheralManager.claimPin(PeripheralOwner::Adc, pin)) {
        taskENTER_CRITICAL();
        s_adcBusy = false;
        taskEXIT_CRITICAL();
        s_adcLastError = CHIPINTELLI_ERROR_BUSY;
        return false;
    }
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
        s_adcIdleTimer == nullptr) {
        s_adcIdleTimer = xTimerCreate(
            "adc-idle", pdMS_TO_TICKS(kAdcIdlePowerDownMs), pdFALSE, nullptr,
            adcIdleTimerCallback);
    }
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
        s_adcTimeoutTimer == nullptr) {
        s_adcTimeoutTimer = xTimerCreate(
            "adc-timeout", pdMS_TO_TICKS(kAdcConversionTimeoutMs), pdFALSE,
            nullptr, adcTimeoutTimerCallback);
    }
    if (s_adcIdleTimer != nullptr) (void)xTimerStop(s_adcIdleTimer, 0U);
    detachInterrupt(pin);
    if (!ensureAdcPower()) {
        (void)pinModeOwned(pin, INPUT, PeripheralOwner::Adc);
        PeripheralManager.releasePin(PeripheralOwner::Adc, pin);
        taskENTER_CRITICAL();
        s_adcBusy = false;
        taskEXIT_CRITICAL();
        return false;
    }
    const PinDescription &desc = g_APinDescription[pin];
    dpmu_set_io_pull(static_cast<PinPad_Name>(desc.pad), DPMU_IO_PULL_DISABLE);
    dpmu_set_adio_reuse(static_cast<PinPad_Name>(desc.pad), ANALOG_MODE);
    s_adcPin = pin;
    s_adcChannel = static_cast<adc_channelx_t>(desc.adcChannel);
    return true;
}

static bool startAdcConversion() {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
        (s_adcTimeoutTimer == nullptr ||
         xTimerChangePeriod(s_adcTimeoutTimer,
                            pdMS_TO_TICKS(kAdcConversionTimeoutMs), 0U) !=
             pdPASS)) {
        s_adcLastError = CHIPINTELLI_ERROR_QUEUE_FULL;
        return false;
    }
    adc_clear_flag();
    adc_int_clear(s_adcChannel);
    adc_convert_config(s_adcChannel, ADC_CLKCYCLE_2);
    adc_continuons_convert(DISABLE);
    adc_int_sel(ADC_INT_MODE_TRANS_END);
    s_adcConversionDone = false;
    s_adcConversionActive = true;
    adc_soc_soft_ctrl(ENABLE);
    return true;
}

static void finishAdcOperation(uint8_t pin) {
    taskENTER_CRITICAL();
    s_adcOperation = AdcOperation::Idle;
    s_adcBusy = false;
    s_adcWaiter = nullptr;
    s_adcCallback = nullptr;
    s_adcCallbackContext = nullptr;
    s_adcPin = kNoAdcPin;
    taskEXIT_CRITICAL();
    (void)pinModeOwned(pin, INPUT, PeripheralOwner::Adc);
    PeripheralManager.releasePin(PeripheralOwner::Adc, pin);
    scheduleAdcPowerDown();
}

static void adcCompletionEvent(void *, uint32_t generation) {
    analog_read_callback_t callback = nullptr;
    void *context = nullptr;
    uint8_t pin = kNoAdcPin;
    int value = 0;
    chipintelli_error_t error = CHIPINTELLI_ERROR_NONE;
    AdcOperation operation = AdcOperation::Idle;
    taskENTER_CRITICAL();
    if (s_adcEventPending && generation == s_adcGeneration) {
        s_adcEventPending = false;
        callback = s_adcCallback;
        context = s_adcCallbackContext;
        pin = s_adcPin;
        operation = s_adcOperation;
        error = s_adcCompletionError;
        value = error == CHIPINTELLI_ERROR_NONE
                    ? static_cast<int>(scaleResolution(
                          s_adcRawValue, 12, s_readResolution))
                    : 0;
    }
    taskEXIT_CRITICAL();
    if (pin == kNoAdcPin) return;
    if (operation == AdcOperation::Asynchronous) finishAdcOperation(pin);
    s_adcLastError = error;
    if (callback != nullptr) {
        callback(pin, value, error, context);
    }
}

static void adcContinuousTimerCallback(TimerHandle_t) {
    taskENTER_CRITICAL();
    const bool start = s_adcOperation == AdcOperation::Continuous &&
                       !s_adcConversionActive && !s_adcEventPending;
    taskEXIT_CRITICAL();
    if (start && !startAdcConversion()) {
        ++s_adcDropped;
    }
}

// The SDK's default vector is weak and empty. Complete the conversion in the
// ISR, then either wake the synchronous caller or defer the user callback.
extern "C" void __wrap_ADC_IRQHandler(void) {
    ADC_irqhandle();
    if (!s_adcConversionActive) return;
    s_adcRawValue = static_cast<uint16_t>(adc_get_result(s_adcChannel) & 0x0fffU);
    s_adcCompletionError = CHIPINTELLI_ERROR_NONE;
    s_adcConversionActive = false;
    s_adcConversionDone = true;

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    if (s_adcTimeoutTimer != nullptr) {
        (void)xTimerStopFromISR(s_adcTimeoutTimer, &higherPriorityTaskWoken);
    }
    if (s_adcOperation == AdcOperation::Synchronous) {
        TaskHandle_t waiter = s_adcWaiter;
        if (waiter != nullptr) {
            vTaskNotifyGiveFromISR(waiter, &higherPriorityTaskWoken);
        }
    } else if (s_adcOperation == AdcOperation::Asynchronous ||
               s_adcOperation == AdcOperation::Continuous) {
        s_adcEventPending = true;
        if (!chipintelli_arduino_post_event_from_isr(
                adcCompletionEvent, nullptr, s_adcGeneration)) {
            s_adcEventPending = false;
            ++s_adcDropped;
            s_adcLastError = CHIPINTELLI_ERROR_QUEUE_FULL;
            if (s_adcOperation == AdcOperation::Asynchronous) {
                s_adcOperation = AdcOperation::Idle;
                s_adcBusy = false;
                s_adcReleasePendingPin = s_adcPin;
                s_adcPin = kNoAdcPin;
                if (s_adcIdleTimer != nullptr) {
                    (void)xTimerResetFromISR(s_adcIdleTimer,
                                             &higherPriorityTaskWoken);
                }
            }
        }
    }
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
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
    if (!prepareAdcPin(pin)) return 0;
    const adc_channelx_t channel = s_adcChannel;
    bool converted = false;
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        TaskHandle_t current = xTaskGetCurrentTaskHandle();
        taskENTER_CRITICAL();
        s_adcOperation = AdcOperation::Synchronous;
        s_adcWaiter = current;
        uint32_t generation = s_adcGeneration + 1U;
        if (generation == 0U) generation = 1U;
        s_adcGeneration = generation;
        taskEXIT_CRITICAL();
        (void)ulTaskNotifyTake(pdTRUE, 0U);
        if (!startAdcConversion()) {
            finishAdcOperation(pin);
            return 0;
        }
        const uint32_t conversionStarted = millis();
        while (!s_adcConversionDone) {
            if (millis() - conversionStarted >= kAdcConversionTimeoutMs) break;
            const uint32_t elapsed = millis() - conversionStarted;
            const uint32_t remaining = kAdcConversionTimeoutMs - elapsed;
            TickType_t wait = pdMS_TO_TICKS(remaining);
            (void)ulTaskNotifyTake(pdTRUE, wait > 0U ? wait : 1U);
            if (s_adcConversionDone) {
                converted = true;
                break;
            }
        }
        converted = s_adcConversionDone;
    } else {
        adc_convert_config(channel, ADC_CLKCYCLE_2);
        adc_continuons_convert(DISABLE);
        adc_int_sel(ADC_INT_MODE_TRANS_END);
        adc_soc_soft_ctrl(ENABLE);
        converted = adc_wait_int_timeout(channel, kAdcHardwareTimeoutUs) ==
                    RETURN_OK;
        if (converted) {
            s_adcRawValue =
                static_cast<uint16_t>(adc_get_result(channel) & 0x0fffU);
        }
    }
    if (!converted) {
        s_adcLastError = CHIPINTELLI_ERROR_TIMEOUT;
        adc_int_clear(channel);
        s_adcConversionActive = false;
        finishAdcOperation(pin);
        return 0;
    }
    const int result = static_cast<int>(
        scaleResolution(s_adcRawValue, 12, s_readResolution));
    finishAdcOperation(pin);
    s_adcLastError = CHIPINTELLI_ERROR_NONE;
    return result;
}

extern "C" chipintelli_error_t analogReadLastError(void) {
    return s_adcLastError;
}

extern "C" bool analogReadAsync(uint8_t pin,
                                 analog_read_callback_t callback,
                                 void *context) {
    if (callback == nullptr ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        s_adcLastError = CHIPINTELLI_ERROR_HARDWARE_FAULT;
        return false;
    }
    if (!prepareAdcPin(pin)) return false;
    taskENTER_CRITICAL();
    uint32_t generation = s_adcGeneration + 1U;
    if (generation == 0U) generation = 1U;
    s_adcGeneration = generation;
    s_adcOperation = AdcOperation::Asynchronous;
    s_adcCallback = callback;
    s_adcCallbackContext = context;
    s_adcEventPending = false;
    taskEXIT_CRITICAL();
    if (!startAdcConversion()) {
        finishAdcOperation(pin);
        return false;
    }
    return true;
}

extern "C" bool analogReadContinuous(uint8_t pin,
                                      uint32_t sampleIntervalMs,
                                      analog_read_callback_t callback,
                                      void *context) {
    if (callback == nullptr || sampleIntervalMs == 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        s_adcLastError = CHIPINTELLI_ERROR_HARDWARE_FAULT;
        return false;
    }
    if (!prepareAdcPin(pin)) return false;
    TickType_t period = pdMS_TO_TICKS(sampleIntervalMs);
    if (period == 0U) period = 1U;
    if (s_adcContinuousTimer == nullptr) {
        s_adcContinuousTimer = xTimerCreate(
            "adc-sample", period, pdTRUE, nullptr,
            adcContinuousTimerCallback);
    }
    if (s_adcContinuousTimer == nullptr ||
        xTimerChangePeriod(s_adcContinuousTimer, period, 0U) != pdPASS) {
        finishAdcOperation(pin);
        s_adcLastError = CHIPINTELLI_ERROR_NO_MEMORY;
        return false;
    }
    taskENTER_CRITICAL();
    uint32_t generation = s_adcGeneration + 1U;
    if (generation == 0U) generation = 1U;
    s_adcGeneration = generation;
    s_adcOperation = AdcOperation::Continuous;
    s_adcCallback = callback;
    s_adcCallbackContext = context;
    s_adcEventPending = false;
    taskEXIT_CRITICAL();
    if (!startAdcConversion()) {
        analogReadContinuousStop();
        return false;
    }
    return true;
}

extern "C" void analogReadContinuousStop(void) {
    if (s_adcContinuousTimer != nullptr) {
        (void)xTimerStop(s_adcContinuousTimer, 0U);
    }
    taskENTER_CRITICAL();
    if (s_adcOperation != AdcOperation::Continuous) {
        taskEXIT_CRITICAL();
        return;
    }
    const uint8_t pin = s_adcPin;
    ++s_adcGeneration;
    s_adcConversionActive = false;
    s_adcEventPending = false;
    taskEXIT_CRITICAL();
    adc_int_clear(s_adcChannel);
    if (pin < NUM_DIGITAL_PINS) finishAdcOperation(pin);
}

extern "C" bool analogReadBusy(void) { return s_adcBusy; }

extern "C" bool analogReadPowerDown(void) {
    releasePendingAdcPin();
    taskENTER_CRITICAL();
    const bool idle = !s_adcBusy && !s_adcConversionActive;
    taskEXIT_CRITICAL();
    if (!idle) {
        s_adcLastError = CHIPINTELLI_ERROR_BUSY;
        return false;
    }
    if (s_adcIdleTimer != nullptr) (void)xTimerStop(s_adcIdleTimer, 0U);
    adcPowerDownNow();
    s_adcLastError = CHIPINTELLI_ERROR_NONE;
    return true;
}

extern "C" uint32_t analogReadDropped(void) {
    taskENTER_CRITICAL();
    const uint32_t dropped = s_adcDropped;
    taskEXIT_CRITICAL();
    return dropped;
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
