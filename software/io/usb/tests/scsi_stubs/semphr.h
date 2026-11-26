#ifndef SCSI_STUB_SEMPHR_H
#define SCSI_STUB_SEMPHR_H
#include "FreeRTOS.h"
// A mutex that the test can make busy or watch: see scsi_sim.h.
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t);
#endif
