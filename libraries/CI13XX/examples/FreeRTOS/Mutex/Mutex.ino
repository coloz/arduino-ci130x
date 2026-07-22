#include <Arduino.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

struct WorkerSettings {
  const char *name;
  uint32_t periodMs;
};

static const WorkerSettings kWorkerA = {"worker A", 200};
static const WorkerSettings kWorkerB = {"worker B", 350};
static SemaphoreHandle_t counterMutex = nullptr;
static uint32_t sharedCounter = 0;

void workerTask(void *parameter) {
  const WorkerSettings *settings =
      static_cast<const WorkerSettings *>(parameter);

  for (;;) {
    if (xSemaphoreTake(counterMutex, portMAX_DELAY) == pdTRUE) {
      ++sharedCounter;
      Serial.printf("%s set counter to %lu\r\n", settings->name,
                    static_cast<unsigned long>(sharedCounter));
      xSemaphoreGive(counterMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(settings->periodMs));
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("FreeRTOS mutex example");

  counterMutex = xSemaphoreCreateMutex();
  if (counterMutex == nullptr) {
    Serial.println("Mutex creation failed");
    return;
  }

  BaseType_t workerACreated =
      xTaskCreate(workerTask, "worker-a", 384,
                  const_cast<WorkerSettings *>(&kWorkerA), 1, nullptr);
  BaseType_t workerBCreated =
      xTaskCreate(workerTask, "worker-b", 384,
                  const_cast<WorkerSettings *>(&kWorkerB), 1, nullptr);
  if (workerACreated != pdPASS || workerBCreated != pdPASS) {
    Serial.println("Task creation failed");
  }
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
