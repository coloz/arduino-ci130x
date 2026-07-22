# FreeRTOS examples

The CI130X Arduino core already builds and starts the FreeRTOS kernel from the
vendor SDK. Sketches can include the SDK headers and call the enabled standard
FreeRTOS V10.1.0 APIs directly; no additional Arduino library is required.

```cpp
#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
```

The examples cover tasks, queues, mutexes and counting semaphores. CI130X is a
single-core target, so use `xTaskCreate()` rather than ESP32-specific APIs such
as `xTaskCreatePinnedToCore()`. The stack-depth argument to `xTaskCreate()` is
measured in 32-bit words on this port.

The scheduler is already running when Arduino calls `setup()`. Do not call
`vTaskStartScheduler()` from a sketch. Keep user task priorities below the
Arduino/SDK tasks unless the application has been designed and tested for a
different priority scheme.

The packaged `FreeRTOSConfig.h` uses a 500 Hz tick, six priorities (0 through
5), dynamic allocation with `heap_4`, and enables tasks, notifications, queues,
mutexes, counting semaphores, event groups and software timers. Static
allocation, recursive mutexes, queue sets and newlib per-task reentrancy are
disabled. Protect shared Arduino objects such as `Serial`, and use
`pdMS_TO_TICKS()` when converting milliseconds to kernel ticks.
