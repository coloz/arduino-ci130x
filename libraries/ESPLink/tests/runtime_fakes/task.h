// SPDX-License-Identifier: MIT
#pragma once
#include "FreeRTOS.h"
BaseType_t xTaskGetSchedulerState(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskCreate(TaskFunction_t, const char*, uint16_t, void*, UBaseType_t, TaskHandle_t*);
void vTaskDelete(TaskHandle_t);
void vTaskDelay(TickType_t);

BaseType_t xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t clearCountOnExit, TickType_t);
