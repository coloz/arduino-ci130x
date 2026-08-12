#include "Arduino.h"
#include "ArduinoEvent.h"
#include "PeripheralManager.h"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "ci130x_gpio.h"
#include "ci130x_dpmu.h"
#include "ci130x_core_eclic.h"
}

struct InterruptHandler {
    voidFuncPtrArg callback;
    void *arg;
    uint32_t generation;
    bool runInIsr;
};

static InterruptHandler s_handlers[NUM_DIGITAL_PINS];
static voidFuncPtr s_simpleHandlers[NUM_DIGITAL_PINS];
static bool s_registered[3];
static gpio_irq_callback_list_t s_nodes[3];
static uint8_t s_registeredBits[3];
static uint8_t s_pinForPortBit[3][8];
static uint32_t s_nextGeneration;

static gpio_base_t portBase(uint8_t port) {
    static const gpio_base_t bases[] = {PA, PB, PC, PD};
    return bases[port < 4 ? port : 0];
}

static void dispatchPort(uint8_t port) {
    if (port >= 3U) return;
    gpio_base_t base = portBase(port);
    uint8_t pending = s_registeredBits[port];
    while (pending != 0U) {
        const uint8_t bit = static_cast<uint8_t>(__builtin_ctz(pending));
        pending = static_cast<uint8_t>(pending & ~(1U << bit));
        const gpio_pin_t mask = static_cast<gpio_pin_t>(1U << bit);
        if (!gpio_get_irq_mask_status_single(base, mask)) continue;

        const uint8_t pin = s_pinForPortBit[port][bit];
        if (pin >= NUM_DIGITAL_PINS) continue;
        const InterruptHandler handler = s_handlers[pin];
        if (handler.callback == nullptr) continue;
        if (handler.runInIsr) {
            handler.callback(handler.arg);
        } else {
            chipintelli_arduino_post_event_from_isr(
                [](void *context, uint32_t generation) {
                    const uint8_t deferredPin = static_cast<uint8_t>(
                        reinterpret_cast<uintptr_t>(context));
                    if (deferredPin >= NUM_DIGITAL_PINS) return;
                    taskENTER_CRITICAL();
                    const InterruptHandler deferred = s_handlers[deferredPin];
                    taskEXIT_CRITICAL();
                    if (!deferred.runInIsr && deferred.callback != nullptr &&
                        deferred.generation == generation) {
                        deferred.callback(deferred.arg);
                    }
                },
                reinterpret_cast<void *>(static_cast<uintptr_t>(pin)),
                handler.generation);
        }
    }
}

static void dispatchA() { dispatchPort(0); }
static void dispatchB() { dispatchPort(1); }
static void dispatchC() { dispatchPort(2); }

static void ensureInterruptPort(uint8_t port) {
    if (port >= 3 || s_registered[port]) return;
    static gpio_irq_callback_t callbacks[] = {dispatchA, dispatchB, dispatchC};
    static const uint32_t irqs[] = {PA_IRQn, PB_IRQn, AON_PC_IRQn};
    s_nodes[port].gpio_irq_callback = callbacks[port];
    s_nodes[port].next = nullptr;
    registe_gpio_callback(portBase(port), &s_nodes[port]);
    eclic_irq_enable(irqs[port]);
    s_registered[port] = true;
}

static void simpleInterruptThunk(void *arg) {
    const uint8_t pin = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(arg));
    if (pin < NUM_DIGITAL_PINS && s_simpleHandlers[pin]) {
        s_simpleHandlers[pin]();
    }
}

static bool configurePinMode(uint8_t pin, uint8_t mode) {
    if (pin >= NUM_DIGITAL_PINS) return false;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_GPIO)) return false;
    gpio_base_t base = portBase(desc.port);
    gpio_pin_t mask = static_cast<gpio_pin_t>(1U << desc.bit);

    scu_set_device_gate(static_cast<uint32_t>(base), ENABLE);
    dpmu_set_adio_reuse(static_cast<PinPad_Name>(desc.pad), DIGITAL_MODE);
    dpmu_set_io_reuse(static_cast<PinPad_Name>(desc.pad), static_cast<IOResue_FUNCTION>(desc.gpioMux));
    dpmu_set_io_open_drain(static_cast<PinPad_Name>(desc.pad), mode == OUTPUT_OPEN_DRAIN ? ENABLE : DISABLE);

    if (mode == OUTPUT || mode == OUTPUT_OPEN_DRAIN) {
        dpmu_set_io_pull(static_cast<PinPad_Name>(desc.pad), DPMU_IO_PULL_DISABLE);
        dpmu_set_io_direction(static_cast<PinPad_Name>(desc.pad), DPMU_IO_DIRECTION_OUTPUT);
        gpio_set_output_mode(base, mask);
    } else {
        Dpmu_Io_Pull_t pull = DPMU_IO_PULL_DISABLE;
        if (mode == INPUT_PULLUP) pull = DPMU_IO_PULL_UP;
        if (mode == INPUT_PULLDOWN) pull = DPMU_IO_PULL_DOWN;
        dpmu_set_io_pull(static_cast<PinPad_Name>(desc.pad), pull);
        dpmu_set_io_direction(static_cast<PinPad_Name>(desc.pad), DPMU_IO_DIRECTION_INPUT);
        gpio_set_input_mode(base, mask);
    }
    return true;
}

bool pinModeOwned(uint8_t pin, uint8_t mode, PeripheralOwner owner) {
    if (pin >= NUM_DIGITAL_PINS ||
        !(g_APinDescription[pin].capabilities & PIN_CAP_GPIO) ||
        !PeripheralManager.claimPin(owner, pin)) {
        return false;
    }
    return configurePinMode(pin, mode);
}

extern "C" void pinMode(uint8_t pin, uint8_t mode) {
    (void)pinModeOwned(pin, mode, PeripheralOwner::GPIO);
}

extern "C" void digitalWrite(uint8_t pin, uint8_t value) {
    if (pin >= NUM_DIGITAL_PINS) return;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_GPIO)) return;
    gpio_set_output_level_single(portBase(desc.port), static_cast<gpio_pin_t>(1U << desc.bit), value != LOW);
}

extern "C" int digitalRead(uint8_t pin) {
    if (pin >= NUM_DIGITAL_PINS) return LOW;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_GPIO)) return LOW;
    return gpio_get_input_level_single(portBase(desc.port), static_cast<gpio_pin_t>(1U << desc.bit)) ? HIGH : LOW;
}

extern "C" void digitalToggle(uint8_t pin) {
    digitalWrite(pin, !digitalRead(pin));
}

static void attachInterruptArgInternal(uint8_t pin, voidFuncPtrArg callback,
                                       void *arg, int mode, bool runInIsr) {
    if (pin >= NUM_DIGITAL_PINS) return;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_INTERRUPT) || desc.port >= 3U ||
        desc.bit >= 8U || !callback) return;
    if (!pinModeOwned(pin, INPUT, PeripheralOwner::GPIO)) return;
    gpio_trigger_t trigger;
    switch (mode) {
        case RISING: trigger = up_edges_trigger; break;
        case FALLING: trigger = down_edges_trigger; break;
        case ONLOW: trigger = low_level_trigger; break;
        case ONHIGH: trigger = high_level_trigger; break;
        case CHANGE:
        default: trigger = both_edges_trigger; break;
    }
    taskENTER_CRITICAL();
    uint32_t generation = ++s_nextGeneration;
    if (generation == 0U) generation = ++s_nextGeneration;
    s_handlers[pin] = {callback, arg, generation, runInIsr};
    s_registeredBits[desc.port] = static_cast<uint8_t>(
        s_registeredBits[desc.port] | (1U << desc.bit));
    s_pinForPortBit[desc.port][desc.bit] = pin;
    taskEXIT_CRITICAL();
    ensureInterruptPort(desc.port);
    gpio_pin_t mask = static_cast<gpio_pin_t>(1U << desc.bit);
    gpio_clear_irq_single(portBase(desc.port), mask);
    gpio_irq_trigger_config(portBase(desc.port), mask, trigger);
}

extern "C" void attachInterruptArg(uint8_t pin, voidFuncPtrArg callback,
                                      void *arg, int mode) {
    attachInterruptArgInternal(pin, callback, arg, mode, false);
}

extern "C" void attachInterruptArgISR(uint8_t pin, voidFuncPtrArg callback,
                                         void *arg, int mode) {
    attachInterruptArgInternal(pin, callback, arg, mode, true);
}

extern "C" void attachInterrupt(uint8_t pin, voidFuncPtr callback, int mode) {
    if (pin >= NUM_DIGITAL_PINS || !callback) return;
    s_simpleHandlers[pin] = callback;
    attachInterruptArg(pin, simpleInterruptThunk,
                       reinterpret_cast<void *>(static_cast<uintptr_t>(pin)), mode);
}

extern "C" void attachInterruptISR(uint8_t pin, voidFuncPtr callback,
                                      int mode) {
    if (pin >= NUM_DIGITAL_PINS || !callback) return;
    s_simpleHandlers[pin] = callback;
    attachInterruptArgISR(
        pin, simpleInterruptThunk,
        reinterpret_cast<void *>(static_cast<uintptr_t>(pin)), mode);
}

extern "C" void detachInterrupt(uint8_t pin) {
    if (pin >= NUM_DIGITAL_PINS) return;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_INTERRUPT) || desc.port >= 3U ||
        desc.bit >= 8U) return;
    gpio_irq_mask(portBase(desc.port), static_cast<gpio_pin_t>(1U << desc.bit));
    taskENTER_CRITICAL();
    s_registeredBits[desc.port] = static_cast<uint8_t>(
        s_registeredBits[desc.port] & ~(1U << desc.bit));
    s_handlers[pin] = {nullptr, nullptr, ++s_nextGeneration, false};
    s_simpleHandlers[pin] = nullptr;
    taskEXIT_CRITICAL();
}
