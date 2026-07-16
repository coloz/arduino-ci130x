#include "Arduino.h"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "ci130x_core_timer.h"
#include "platform_config.h"
}

static uint64_t timerToMicros(uint64_t ticks) {
    const uint32_t frequency = get_systick_clk();
    if (!frequency) return 0;
    return (ticks / frequency) * 1000000ULL +
           ((ticks % frequency) * 1000000ULL) / frequency;
}

extern "C" unsigned long micros(void) {
    return static_cast<unsigned long>(timerToMicros(get_timer_value()));
}

extern "C" unsigned long millis(void) {
    return static_cast<unsigned long>(timerToMicros(get_timer_value()) / 1000ULL);
}

extern "C" void delayMicroseconds(unsigned int us) {
    const uint64_t start = get_timer_value();
    const uint32_t frequency = get_systick_clk();
    const uint64_t ticks = (static_cast<uint64_t>(frequency) * us + 999999ULL) / 1000000ULL;
    while ((get_timer_value() - start) < ticks) {}
}

extern "C" void delay(unsigned long ms) {
    if (ms == 0) {
        yield();
        return;
    }
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && ms >= portTICK_PERIOD_MS) {
        TickType_t ticks = static_cast<TickType_t>(
            (static_cast<uint64_t>(ms) + portTICK_PERIOD_MS - 1U) / portTICK_PERIOD_MS);
        vTaskDelay(ticks ? ticks : 1);
    } else {
        while (ms--) delayMicroseconds(1000);
    }
}

extern "C" void yield(void) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        // Blocking for one tick also gives the lower-priority idle task a
        // chance to reclaim deleted FreeRTOS tasks and perform housekeeping.
        vTaskDelay(1);
    }
}

extern "C" void noInterrupts(void) {
    portDISABLE_INTERRUPTS();
}

extern "C" void interrupts(void) {
    portENABLE_INTERRUPTS();
}
