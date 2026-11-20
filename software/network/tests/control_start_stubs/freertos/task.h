#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
BaseType_t xTaskCreate(TaskFunction_t,const char*,unsigned,void*,UBaseType_t,TaskHandle_t*);
void vTaskDelay(TickType_t);
