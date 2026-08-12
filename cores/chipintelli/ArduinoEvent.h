#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*chipintelli_arduino_event_callback_t)(void *context,
                                                      uint32_t value);

// Both post functions are zero-wait. A false return means that the fixed
// dispatcher queue is full; the global dropped-event counter is incremented.
bool chipintelli_arduino_post_event(
    chipintelli_arduino_event_callback_t callback, void *context,
    uint32_t value);
bool chipintelli_arduino_post_event_from_isr(
    chipintelli_arduino_event_callback_t callback, void *context,
    uint32_t value);

// Wake setup()/loop() without enqueueing a callback. Drivers use this for
// readiness notifications such as UART RX and ASR result queues.
void chipintelli_arduino_wake(void);
void chipintelli_arduino_wake_from_isr(void);

uint32_t chipintelli_arduino_event_dropped(void);
size_t chipintelli_arduino_event_pending(void);
size_t chipintelli_arduino_event_high_water_mark(void);
void chipintelli_arduino_event_clear_stats(void);

// Core-internal scheduler hooks. Libraries should normally use post/wake.
void chipintelli_arduino_event_set_task(void *task);
size_t chipintelli_arduino_dispatch_events(size_t max_events,
                                           uint32_t budget_us);

#ifdef __cplusplus
}
#endif
