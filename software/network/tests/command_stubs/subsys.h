#pragma once
#include <stdint.h>
#include <string.h>
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
#define pdFALSE 0
#define pdTRUE 1
#define portMAX_DELAY 0xffffffff
#define configMINIMAL_STACK_SIZE 256
#define PRIO_HW_SERVICE 1
#define SUBSYSID_CMD_IF 1
class SubSystem { public: explicit SubSystem(int) {} };
void enter_critical();
void exit_critical();
#define portENTER_CRITICAL() enter_critical()
#define portEXIT_CRITICAL() exit_critical()
QueueHandle_t xQueueCreate(int, int);
BaseType_t xQueueSendFromISR(QueueHandle_t, const void *, BaseType_t *);
void xQueueReceive(QueueHandle_t, void *, unsigned);
void xTaskCreate(void (*)(void *), const char *, int, void *, int, TaskHandle_t *);
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return 0; }
inline void xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t *) {}
inline void xSemaphoreTake(SemaphoreHandle_t, unsigned) {}
