#include "utility/SdLock.h"

extern "C" {
#include "FreeRTOS.h"
#include "ci130x_core_misc.h"
#include "semphr.h"
#include "task.h"
}

namespace SDLib {
namespace detail {
namespace {

SemaphoreHandle_t s_fileSystemMutex = nullptr;

SemaphoreHandle_t fileSystemMutex() {
  if (check_curr_trap() != 0) {
    return nullptr;
  }

  taskENTER_CRITICAL();
  SemaphoreHandle_t mutex = s_fileSystemMutex;
  taskEXIT_CRITICAL();
  if (mutex != nullptr) {
    return mutex;
  }

  SemaphoreHandle_t created = xSemaphoreCreateMutex();
  if (created == nullptr) {
    return nullptr;
  }

  taskENTER_CRITICAL();
  if (s_fileSystemMutex == nullptr) {
    s_fileSystemMutex = created;
    created = nullptr;
  }
  mutex = s_fileSystemMutex;
  taskEXIT_CRITICAL();

  if (created != nullptr) {
    vSemaphoreDelete(created);
  }
  return mutex;
}

}  // namespace

bool lockFileSystem() {
  SemaphoreHandle_t mutex = fileSystemMutex();
  if (mutex == nullptr) {
    return false;
  }
  const TickType_t wait =
      xTaskGetSchedulerState() == taskSCHEDULER_RUNNING ? portMAX_DELAY : 0U;
  return xSemaphoreTake(mutex, wait) == pdTRUE;
}

void unlockFileSystem() {
  if (s_fileSystemMutex != nullptr) {
    xSemaphoreGive(s_fileSystemMutex);
  }
}

}  // namespace detail
}  // namespace SDLib
