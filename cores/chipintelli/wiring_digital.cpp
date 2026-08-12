#include "Arduino.h"
#include "ArduinoEvent.h"
#include "PeripheralManager.h"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "ci130x_gpio.h"
#include "ci130x_dpmu.h"
#include "ci130x_core_eclic.h"
#include "ci130x_core_timer.h"
}

struct InterruptHandler {
    voidFuncPtrArg callback;
    chipintelli_gpio_event_callback_t eventCallback;
    void *arg;
    uint32_t generation;
    int mode;
    bool runInIsr;
};

struct GpioRegisters {
    volatile uint32_t data[256];
    volatile uint32_t direction;
    volatile uint32_t sense;
    volatile uint32_t bothEdges;
    volatile uint32_t event;
    volatile uint32_t interruptEnable;
    volatile uint32_t rawInterruptStatus;
    volatile uint32_t maskedInterruptStatus;
    volatile uint32_t interruptClear;
    volatile uint32_t alternateFunction;
};

struct DeferredGpioEvent {
    chipintelli_gpio_event_t event;
    uint32_t generation;
    volatile bool used;
};

static_assert(offsetof(GpioRegisters, maskedInterruptStatus) == 0x418,
              "CI130X GPIO MIS offset mismatch");
static_assert(offsetof(GpioRegisters, interruptClear) == 0x41c,
              "CI130X GPIO IC offset mismatch");

constexpr uint8_t kDeferredGpioEventCount = 24U;

static InterruptHandler s_handlers[NUM_DIGITAL_PINS];
static voidFuncPtr s_simpleHandlers[NUM_DIGITAL_PINS];
static bool s_portEnabled[3];
static uint8_t s_registeredBits[3];
static uint8_t s_pinForPortBit[3][8];
static uint32_t s_nextGeneration;
static DeferredGpioEvent s_deferredEvents[kDeferredGpioEventCount];
static volatile uint32_t s_gpioDispatched;
static volatile uint32_t s_gpioDropped;
static volatile uint64_t s_gpioMaxIsrTicks;

static gpio_base_t portBase(uint8_t port) {
    static const gpio_base_t bases[] = {PA, PB, PC, PD};
    return bases[port < 4 ? port : 0];
}

static chipintelli_gpio_edge_t edgeForHandler(const InterruptHandler &handler,
                                               bool levelHigh) {
    switch (handler.mode) {
        case RISING: return CHIPINTELLI_GPIO_EDGE_RISING;
        case FALLING: return CHIPINTELLI_GPIO_EDGE_FALLING;
        case ONHIGH: return CHIPINTELLI_GPIO_LEVEL_HIGH;
        case ONLOW: return CHIPINTELLI_GPIO_LEVEL_LOW;
        case CHANGE:
        default:
            return levelHigh ? CHIPINTELLI_GPIO_EDGE_RISING
                             : CHIPINTELLI_GPIO_EDGE_FALLING;
    }
}

static void deferredGpioEvent(void *context, uint32_t) {
    const uint8_t slot = static_cast<uint8_t>(
        reinterpret_cast<uintptr_t>(context));
    if (slot >= kDeferredGpioEventCount) return;
    taskENTER_CRITICAL();
    const DeferredGpioEvent record = s_deferredEvents[slot];
    s_deferredEvents[slot].used = false;
    InterruptHandler handler = {};
    if (record.event.pin < NUM_DIGITAL_PINS) {
        handler = s_handlers[record.event.pin];
    }
    taskEXIT_CRITICAL();
    if (handler.runInIsr || handler.generation != record.generation) return;
    if (handler.eventCallback != nullptr) {
        handler.eventCallback(&record.event, handler.arg);
    } else if (handler.callback != nullptr) {
        handler.callback(handler.arg);
    }
}

extern "C" uint8_t chipintelli_gpio_irq_dispatch(uint32_t base,
                                                   int portIndex,
                                                   uint8_t pending) {
    if (portIndex < 0 || portIndex >= 3) return 0U;
    const uint64_t entered = get_timer_value();
    GpioRegisters *const registers =
        reinterpret_cast<GpioRegisters *>(static_cast<uintptr_t>(base));
    uint8_t handled = static_cast<uint8_t>(
        pending & s_registeredBits[portIndex]);
    if (handled == 0U) return 0U;

    // The SDK captured MIS once and passed it here. Clear the whole Arduino
    // subset in one register write before callbacks can retrigger the port.
    registers->interruptClear = handled;
    const uint8_t levels = static_cast<uint8_t>(registers->data[0xffU]);
    const uint32_t timestamp = micros();
    uint8_t bits = handled;
    while (bits != 0U) {
        const uint8_t bit = static_cast<uint8_t>(__builtin_ctz(bits));
        bits = static_cast<uint8_t>(bits & ~(1U << bit));
        const uint8_t pin = s_pinForPortBit[portIndex][bit];
        if (pin >= NUM_DIGITAL_PINS) continue;
        const InterruptHandler handler = s_handlers[pin];
        if (handler.callback == nullptr && handler.eventCallback == nullptr) {
            continue;
        }
        ++s_gpioDispatched;
        if (handler.runInIsr) {
            handler.callback(handler.arg);
            continue;
        }

        uint8_t slot = kDeferredGpioEventCount;
        const UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
        for (uint8_t i = 0U; i < kDeferredGpioEventCount; ++i) {
            if (!s_deferredEvents[i].used) {
                s_deferredEvents[i].used = true;
                slot = i;
                break;
            }
        }
        taskEXIT_CRITICAL_FROM_ISR(saved);
        if (slot == kDeferredGpioEventCount) {
            ++s_gpioDropped;
            continue;
        }
        s_deferredEvents[slot].event = {
            pin, edgeForHandler(handler, (levels & (1U << bit)) != 0U),
            timestamp};
        s_deferredEvents[slot].generation = handler.generation;
        if (!chipintelli_arduino_post_event_from_isr(
                deferredGpioEvent,
                reinterpret_cast<void *>(static_cast<uintptr_t>(slot)), 0U)) {
            s_deferredEvents[slot].used = false;
            ++s_gpioDropped;
        }
    }
    const uint64_t duration = get_timer_value() - entered;
    if (duration > s_gpioMaxIsrTicks) s_gpioMaxIsrTicks = duration;
    return handled;
}

static void ensureInterruptPort(uint8_t port) {
    if (port >= 3 || s_portEnabled[port]) return;
    static const uint32_t irqs[] = {PA_IRQn, PB_IRQn, AON_PC_IRQn};
    eclic_irq_enable(irqs[port]);
    s_portEnabled[port] = true;
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
                                       chipintelli_gpio_event_callback_t eventCallback,
                                       void *arg, int mode, bool runInIsr) {
    if (pin >= NUM_DIGITAL_PINS) return;
    const PinDescription &desc = g_APinDescription[pin];
    if (!(desc.capabilities & PIN_CAP_INTERRUPT) || desc.port >= 3U ||
        desc.bit >= 8U || (callback == nullptr && eventCallback == nullptr) ||
        (runInIsr && callback == nullptr)) return;
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
    s_handlers[pin] = {callback, eventCallback, arg, generation, mode,
                       runInIsr};
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
    attachInterruptArgInternal(pin, callback, nullptr, arg, mode, false);
}

extern "C" void attachInterruptArgISR(uint8_t pin, voidFuncPtrArg callback,
                                         void *arg, int mode) {
    attachInterruptArgInternal(pin, callback, nullptr, arg, mode, true);
}

extern "C" void attachInterruptEvent(
    uint8_t pin, chipintelli_gpio_event_callback_t callback, void *context,
    int mode) {
    attachInterruptArgInternal(pin, nullptr, callback, context, mode, false);
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
    s_handlers[pin] = {nullptr, nullptr, nullptr, ++s_nextGeneration, CHANGE,
                       false};
    s_simpleHandlers[pin] = nullptr;
    taskEXIT_CRITICAL();
}

extern "C" chipintelli_gpio_interrupt_stats_t gpioInterruptStats(void) {
    taskENTER_CRITICAL();
    const uint32_t dispatched = s_gpioDispatched;
    const uint32_t dropped = s_gpioDropped;
    const uint64_t maxTicks = s_gpioMaxIsrTicks;
    taskEXIT_CRITICAL();
    const uint32_t frequency = get_systick_clk();
    const uint32_t maxMicros = frequency == 0U
        ? 0U
        : static_cast<uint32_t>(
              (maxTicks / frequency) * 1000000ULL +
              ((maxTicks % frequency) * 1000000ULL) / frequency);
    return {dispatched, dropped, maxMicros};
}

extern "C" void gpioInterruptClearStats(void) {
    taskENTER_CRITICAL();
    s_gpioDispatched = 0U;
    s_gpioDropped = 0U;
    s_gpioMaxIsrTicks = 0U;
    taskEXIT_CRITICAL();
}
