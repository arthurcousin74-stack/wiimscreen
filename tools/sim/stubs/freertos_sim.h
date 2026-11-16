// Simulator: FreeRTOS calls mapped to std::thread / std::mutex.
#pragma once

#include <cstddef>
#include <cstdint>

typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef void (*TaskFunction_t)(void*);

#define portMAX_DELAY 0xFFFFFFFFu
#define pdPASS 1
#define pdFAIL 0
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

SemaphoreHandle_t xSemaphoreCreateMutex();
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t m);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack, void* arg, UBaseType_t prio,
                                   TaskHandle_t* out, BaseType_t core);
inline void vTaskDelete(TaskHandle_t) {}  // the thread ends when its function returns
void vTaskDelay(TickType_t ticks);
void xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 0; }
