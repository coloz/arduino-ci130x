#include <Arduino.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

struct TaskSettings {
  const char *name;
  uint32_t periodMs;
};

static const TaskSettings kFastTask = {"fast", 500};
static const TaskSettings kSlowTask = {"slow", 1000};
static SemaphoreHandle_t serialMutex = nullptr;

void periodicTask(void *parameter) {
  const TaskSettings *settings = static_cast<const TaskSettings *>(parameter);
  uint32_t count = 0;

  for (;;) {
    if (xSemaphoreTake(serialMutex, portMAX_DELAY) == pdTRUE) {
      Serial.printf("%s task: %lu\r\n", settings->name,
                    static_cast<unsigned long>(++count));
      xSemaphoreGive(serialMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(settings->periodMs));
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("FreeRTOS basic multitasking example");

  // HardwareSerial uses polling, so serialize complete messages from tasks.
  serialMutex = xSemaphoreCreateMutex();
  if (serialMutex == nullptr) {
    Serial.println("Serial mutex creation failed");
    return;
  }

  // Stack depth is specified in 32-bit words on CI130X (384 words = 1536 B).
  BaseType_t fastCreated =
      xTaskCreate(periodicTask, "fast", 384, const_cast<TaskSettings *>(&kFastTask),
                  2, nullptr);
  BaseType_t slowCreated =
      xTaskCreate(periodicTask, "slow", 384, const_cast<TaskSettings *>(&kSlowTask),
                  1, nullptr);

  if (fastCreated != pdPASS || slowCreated != pdPASS) {
    Serial.println("Task creation failed");
  }
}

void loop() {
  // setup() and loop() already run in an Arduino FreeRTOS task. Blocking here
  // leaves the processor available to the two tasks created above.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
