// Minimal FreeRTOS surface for the UCI target harness. Semaphores, tasks and
// queues are no-ops; nothing in the three production files under test blocks.
#ifndef UCI_STUB_RTOS_H
#define UCI_STUB_RTOS_H
#include <stdint.h>
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define configMINIMAL_STACK_SIZE 100
#define PRIO_BACKGROUND 0
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)1; }
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return (SemaphoreHandle_t)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
static inline int xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t) { return 1; }
static inline int xSemaphoreGiveRecursive(SemaphoreHandle_t) { return 1; }
static inline void vSemaphoreDelete(SemaphoreHandle_t) { }
static inline int xTaskCreate(void (*)(void *), const char *, int, void *, int, TaskHandle_t *) { return 1; }
static inline void vTaskDelay(TickType_t) { }
static inline void vTaskDelete(TaskHandle_t) { }
#endif
