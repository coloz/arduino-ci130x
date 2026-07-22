#include <Arduino.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

static const uint32_t kProducerPeriodsMs[] = {700, 1100};
static SemaphoreHandle_t eventSemaphore = nullptr;

void producerTask(void *parameter) {
  const uint32_t periodMs = *static_cast<const uint32_t *>(parameter);

  for (;;) {
    // A counting semaphore remembers up to eight pending events.
    xSemaphoreGive(eventSemaphore);
    vTaskDelay(pdMS_TO_TICKS(periodMs));
  }
}

void consumerTask(void *) {
  uint32_t receivedEvents = 0;

  for (;;) {
    if (xSemaphoreTake(eventSemaphore, portMAX_DELAY) == pdTRUE) {
      Serial.printf("Consumed event %lu\r\n",
                    static_cast<unsigned long>(++receivedEvents));
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("FreeRTOS counting semaphore example");

  eventSemaphore = xSemaphoreCreateCounting(8, 0);
  if (eventSemaphore == nullptr) {
    Serial.println("Semaphore creation failed");
    return;
  }

  BaseType_t producerACreated =
      xTaskCreate(producerTask, "producer-a", 256,
                  const_cast<uint32_t *>(&kProducerPeriodsMs[0]), 1, nullptr);
  BaseType_t producerBCreated =
      xTaskCreate(producerTask, "producer-b", 256,
                  const_cast<uint32_t *>(&kProducerPeriodsMs[1]), 1, nullptr);
  BaseType_t consumerCreated =
      xTaskCreate(consumerTask, "consumer", 384, nullptr, 2, nullptr);
  if (producerACreated != pdPASS || producerBCreated != pdPASS ||
      consumerCreated != pdPASS) {
    Serial.println("Task creation failed");
  }
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
