/* Stand-in for task.h in the host test of software/system/assert.c. */
#ifndef RTOS_DIAG_TASK_H
#define RTOS_DIAG_TASK_H

#include "FreeRTOS.h"

typedef enum { eRunning = 0, eReady, eBlocked, eSuspended, eDeleted, eInvalid } eTaskState;

typedef struct xTASK_STATUS {
    TaskHandle_t xHandle;
    const char *pcTaskName;
    UBaseType_t xTaskNumber;
    eTaskState eCurrentState;
    UBaseType_t uxCurrentPriority;
    UBaseType_t uxBasePriority;
    uint32_t ulRunTimeCounter;
    StackType_t *pxStackBase;
    uint16_t usStackHighWaterMark;
} TaskStatus_t;

UBaseType_t uxTaskGetNumberOfTasks(void);
UBaseType_t uxTaskGetSystemState(TaskStatus_t *pxTaskStatusArray, UBaseType_t uxArraySize,
                                 uint32_t *pulTotalRunTime);
void vTaskList(char *pcWriteBuffer);

#endif
