#include "ArduinoEvent.h"

#include "Arduino.h"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

#ifndef CHIPINTELLI_ARDUINO_EVENT_QUEUE_SIZE
#define CHIPINTELLI_ARDUINO_EVENT_QUEUE_SIZE 32U
#endif

static_assert(CHIPINTELLI_ARDUINO_EVENT_QUEUE_SIZE >= 4U,
              "Arduino event queue must contain at least four entries");
static_assert(CHIPINTELLI_ARDUINO_EVENT_QUEUE_SIZE <= UINT16_MAX,
              "Arduino event queue is too large");

namespace {
struct ArduinoEvent {
  chipintelli_arduino_event_callback_t callback;
  void *context;
  uint32_t value;
};

ArduinoEvent s_events[CHIPINTELLI_ARDUINO_EVENT_QUEUE_SIZE] = {};
volatile uint16_t s_head = 0U;
volatile uint16_t s_tail = 0U;
volatile uint16_t s_count = 0U;
volatile uint16_t s_highWater = 0U;
volatile uint32_t s_dropped = 0U;
TaskHandle_t s_arduinoTask = nullptr;

void notifyArduinoTask() {
  TaskHandle_t task = s_arduinoTask;
  if (task != nullptr && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
      xTaskGetCurrentTaskHandle() != task) {
    xTaskNotifyGive(task);
  }
}

void notifyArduinoTaskFromIsr() {
  TaskHandle_t task = s_arduinoTask;
  if (task == nullptr) return;
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(task, &higherPriorityTaskWoken);
  portYIELD_FROM_ISR(higherPriorityTaskWoken);
}
}  // namespace

extern "C" bool chipintelli_arduino_post_event(
    chipintelli_arduino_event_callback_t callback, void *context,
    uint32_t value) {
  if (callback == nullptr) return false;

  bool accepted = false;
  taskENTER_CRITICAL();
  if (s_count < CHIPINTELLI_ARDUINO_EVENT_QUEUE_SIZE) {
    s_events[s_head] = {callback, context, value};
    s_head = static_cast<uint16_t>(
        (s_head + 1U) % CHIPINTELLI_ARDUINO_EVENT_QUEUE_SIZE);
    ++s_count;
    if (s_count > s_highWater) s_highWater = s_count;
    accepted = true;
  } else {
    ++s_dropped;
  }
  taskEXIT_CRITICAL();

  if (accepted) notifyArduinoTask();
  return accepted;
}

extern "C" bool chipintelli_arduino_post_event_from_isr(
    chipintelli_arduino_event_callback_t callback, void *context,
    uint32_t value) {
  if (callback == nullptr) return false;

  bool accepted = false;
  const UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
  if (s_count < CHIPINTELLI_ARDUINO_EVENT_QUEUE_SIZE) {
    s_events[s_head] = {callback, context, value};
    s_head = static_cast<uint16_t>(
        (s_head + 1U) % CHIPINTELLI_ARDUINO_EVENT_QUEUE_SIZE);
    ++s_count;
    if (s_count > s_highWater) s_highWater = s_count;
    accepted = true;
  } else {
    ++s_dropped;
  }
  taskEXIT_CRITICAL_FROM_ISR(saved);

  if (accepted) notifyArduinoTaskFromIsr();
  return accepted;
}

extern "C" void chipintelli_arduino_wake(void) { notifyArduinoTask(); }

extern "C" void chipintelli_arduino_wake_from_isr(void) {
  notifyArduinoTaskFromIsr();
}

extern "C" void chipintelli_arduino_event_set_task(void *task) {
  taskENTER_CRITICAL();
  s_arduinoTask = static_cast<TaskHandle_t>(task);
  taskEXIT_CRITICAL();
}

extern "C" size_t chipintelli_arduino_dispatch_events(size_t maxEvents,
                                                        uint32_t budgetUs) {
  if (maxEvents == 0U) return 0U;
  const uint32_t started = micros();
  size_t dispatched = 0U;

  while (dispatched < maxEvents) {
    ArduinoEvent event = {};
    taskENTER_CRITICAL();
    if (s_count == 0U) {
      taskEXIT_CRITICAL();
      break;
    }
    event = s_events[s_tail];
    s_tail = static_cast<uint16_t>(
        (s_tail + 1U) % CHIPINTELLI_ARDUINO_EVENT_QUEUE_SIZE);
    --s_count;
    taskEXIT_CRITICAL();

    event.callback(event.context, event.value);
    ++dispatched;
    if (budgetUs != 0U &&
        static_cast<uint32_t>(micros() - started) >= budgetUs) {
      break;
    }
  }
  return dispatched;
}

extern "C" uint32_t chipintelli_arduino_event_dropped(void) {
  taskENTER_CRITICAL();
  const uint32_t result = s_dropped;
  taskEXIT_CRITICAL();
  return result;
}

extern "C" size_t chipintelli_arduino_event_pending(void) {
  taskENTER_CRITICAL();
  const size_t result = s_count;
  taskEXIT_CRITICAL();
  return result;
}

extern "C" size_t chipintelli_arduino_event_high_water_mark(void) {
  taskENTER_CRITICAL();
  const size_t result = s_highWater;
  taskEXIT_CRITICAL();
  return result;
}

extern "C" void chipintelli_arduino_event_clear_stats(void) {
  taskENTER_CRITICAL();
  s_dropped = 0U;
  s_highWater = s_count;
  taskEXIT_CRITICAL();
}
