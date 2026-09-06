#pragma once

#include <stdint.h>

extern "C" {
unsigned long millis(void);
void delay(unsigned long milliseconds);

typedef uint8_t chipintelli_sdk_state_t;
enum {
  CHIPINTELLI_SDK_NOT_STARTED = 0,
  CHIPINTELLI_SDK_STARTING,
  CHIPINTELLI_SDK_READY,
  CHIPINTELLI_SDK_FAILED
};
bool chipintelli_sdk_begin(void);
chipintelli_sdk_state_t chipintelli_sdk_state(void);
}
