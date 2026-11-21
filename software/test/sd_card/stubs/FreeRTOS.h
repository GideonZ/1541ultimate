#ifndef SD_STUB_FREERTOS_H
#define SD_STUB_FREERTOS_H
#include <stdint.h>
typedef void *SemaphoreHandle_t;
typedef uint32_t TickType_t;
typedef long BaseType_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define taskSCHEDULER_RUNNING 2
SemaphoreHandle_t xSemaphoreCreateMutex(void);
void vSemaphoreDelete(SemaphoreHandle_t);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t);
BaseType_t xTaskGetSchedulerState(void);
// Every way a task can give the CPU up from inside the driver lands in one hook, which the
// test counts as a failure (the waits must not yield or block: see sd_card_test.cc).
void sd_sim_yield(void);
#define taskYIELD() sd_sim_yield()
#define portYIELD() sd_sim_yield()
void vTaskDelay(TickType_t);
void vTaskDelayUntil(TickType_t *, TickType_t);
BaseType_t xTaskDelayUntil(TickType_t *, TickType_t);
#endif
