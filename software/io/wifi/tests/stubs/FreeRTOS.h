// Stand-in for the Ultimate's FreeRTOS on the build host: the types and the few calls the
// WiFi application makes (see wifi_task_env.h)
#ifndef WIFI_TESTS_FREERTOS_H
#define WIFI_TESTS_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
#define pdTRUE  1
#define pdFALSE 0
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFu)
#define configTICK_RATE_HZ 200
#define pdMS_TO_TICKS(ms) ((TickType_t)(((TickType_t)(ms) * configTICK_RATE_HZ) / 1000))
#define ENTER_SAFE_SECTION
#define LEAVE_SAFE_SECTION
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t t);
void vTaskSuspend(TaskHandle_t t);
#endif
