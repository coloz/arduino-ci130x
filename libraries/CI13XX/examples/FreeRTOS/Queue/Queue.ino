#include <Arduino.h>
#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

static QueueHandle_t messageQueue = nullptr;

void producerTask(void *) {
  uint32_t value = 0;

  for (;;) {
    ++value;
    // Block if the queue is full. The consumer is the only task that writes to
    // Serial, so complete output lines cannot be interleaved.
    xQueueSend(messageQueue, &value, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void consumerTask(void *) {
  uint32_t value = 0;

  for (;;) {
    if (xQueueReceive(messageQueue, &value, portMAX_DELAY) == pdPASS) {
      Serial.printf("Received: %lu\r\n", static_cast<unsigned long>(value));
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("FreeRTOS queue example");

  // Queue items are copied into the queue, so a pointer to a local value is
  // safe to pass to xQueueSend().
  messageQueue = xQueueCreate(5, sizeof(uint32_t));
  if (messageQueue == nullptr) {
    Serial.println("Queue creation failed");
    return;
  }

  BaseType_t producerCreated =
      xTaskCreate(producerTask, "producer", 384, nullptr, 2, nullptr);
  BaseType_t consumerCreated =
      xTaskCreate(consumerTask, "consumer", 384, nullptr, 1, nullptr);
  if (producerCreated != pdPASS || consumerCreated != pdPASS) {
    Serial.println("Task creation failed");
  }
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
