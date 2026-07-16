#include "Arduino.h"

extern "C" {
#include "ci130x_gpio.h"
#include "ci130x_dpmu.h"
#include "ci130x_core_eclic.h"
}

struct InterruptHandler {
    voidFuncPtrArg callback;
    void *arg;
};

static InterruptHandler s_handlers[NUM_DIGITAL_PINS];
static voidFuncPtr s_simpleHandlers[NUM_DIGITAL_PINS];
static bool s_registered[3];
static gpio_irq_callback_list_t s_nodes[3];

static gpio_base_t portBase(uint8_t port) {
    static const gpio_base_t bases[] = {PA, PB, PC, PD};
    return bases[port < 4 ? port : 0];
}

static void dispatchPort(uint8_t port) {
    gpio_base_t base = portBase(port);
    for (uint8_t pin = 0; pin < NUM_DIGITAL_PINS; ++pin) {
        const PinDescription &desc = g_APinDescription[pin];
        if (desc.port != port || !s_handlers[pin].callback) continue;
        gpio_pin_t mask = static_cast<gpio_pin_t>(1U << desc.bit);
        if (gpio_get_irq_mask_status_single(base, mask)) {
            s_handlers[pin].callback(s_handlers[pin].arg);
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

extern "C" void pinMode(uint8_t pin, uint8_t mode) {
    if (pin >= NUM_DIGITAL_PINS) return;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_GPIO)) return;
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

extern "C" void attachInterruptArg(uint8_t pin, voidFuncPtrArg callback, void *arg, int mode) {
    if (pin >= NUM_DIGITAL_PINS) return;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_INTERRUPT) || !callback) return;
    gpio_trigger_t trigger;
    switch (mode) {
        case RISING: trigger = up_edges_trigger; break;
        case FALLING: trigger = down_edges_trigger; break;
        case ONLOW: trigger = low_level_trigger; break;
        case ONHIGH: trigger = high_level_trigger; break;
        case CHANGE:
        default: trigger = both_edges_trigger; break;
    }
    s_handlers[pin] = {callback, arg};
    ensureInterruptPort(desc.port);
    gpio_pin_t mask = static_cast<gpio_pin_t>(1U << desc.bit);
    gpio_clear_irq_single(portBase(desc.port), mask);
    gpio_irq_trigger_config(portBase(desc.port), mask, trigger);
}

extern "C" void attachInterrupt(uint8_t pin, voidFuncPtr callback, int mode) {
    if (pin >= NUM_DIGITAL_PINS || !callback) return;
    s_simpleHandlers[pin] = callback;
    attachInterruptArg(pin, simpleInterruptThunk,
                       reinterpret_cast<void *>(static_cast<uintptr_t>(pin)), mode);
}

extern "C" void detachInterrupt(uint8_t pin) {
    if (pin >= NUM_DIGITAL_PINS) return;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_INTERRUPT)) return;
    gpio_irq_mask(portBase(desc.port), static_cast<gpio_pin_t>(1U << desc.bit));
    s_handlers[pin] = {nullptr, nullptr};
    s_simpleHandlers[pin] = nullptr;
}
