// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
#define portTICK_PERIOD_MS 2U
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define taskSCHEDULER_RUNNING 2

#define configMAX_PRIORITIES 6
