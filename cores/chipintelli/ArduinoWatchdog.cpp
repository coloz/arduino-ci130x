#include "Arduino.h"

extern "C" {
#include "FreeRTOS.h"
#include "ci130x_iwdg.h"
#include "task.h"
}

namespace {
volatile uint32_t s_requiredLiveness = 0U;
volatile uint32_t s_observedLiveness = 0U;
// The vendor startup enables IWDG before FreeRTOS starts. ChipIntelliWatchdog
// can explicitly disable it later, in which case the permanent audio task must
// stop touching the gated peripheral.
volatile bool s_feedEnabled = true;
}

extern "C" void chipintelli_watchdog_set_feed_enabled(bool enabled) {
  taskENTER_CRITICAL();
  s_feedEnabled = enabled;
  if (!enabled) s_observedLiveness = 0U;
  taskEXIT_CRITICAL();
}

extern "C" void chipintelli_watchdog_set_liveness_mask(uint32_t mask) {
  taskENTER_CRITICAL();
  s_requiredLiveness = mask;
  s_observedLiveness = 0U;
  taskEXIT_CRITICAL();
}

extern "C" uint32_t chipintelli_watchdog_liveness_mask(void) {
  taskENTER_CRITICAL();
  const uint32_t mask = s_requiredLiveness;
  taskEXIT_CRITICAL();
  return mask;
}

extern "C" void chipintelli_watchdog_heartbeat(uint32_t sources) {
  taskENTER_CRITICAL();
  if (!s_feedEnabled) {
    taskEXIT_CRITICAL();
    return;
  }
  const uint32_t required = s_requiredLiveness;
  if (required == 0U) {
    iwdg_feed(IWDG);
  } else {
    s_observedLiveness |= sources;
    if ((s_observedLiveness & required) == required) {
      s_observedLiveness = 0U;
      iwdg_feed(IWDG);
    }
  }
  taskEXIT_CRITICAL();
}

// The vendor audio task used to feed IWDG unconditionally. In supervised
// mode it contributes only the SDK-audio bit, so a stalled Arduino loop or
// application task can no longer be hidden by a healthy capture task.
extern "C" void chipintelli_watchdog_sdk_audio_heartbeat(void) {
  taskENTER_CRITICAL();
  if (!s_feedEnabled) {
    taskEXIT_CRITICAL();
    return;
  }
  const uint32_t required = s_requiredLiveness;
  if (required == 0U) {
    iwdg_feed(IWDG);
  } else {
    s_observedLiveness |= (1UL << 1);
    if ((s_observedLiveness & required) == required) {
      s_observedLiveness = 0U;
      iwdg_feed(IWDG);
    }
  }
  taskEXIT_CRITICAL();
}
