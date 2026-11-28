#include "FreeRTOS.h"
extern "C" {
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);
}
