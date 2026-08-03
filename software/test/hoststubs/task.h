#ifndef TEST_HOSTSTUBS_TASK_H
#define TEST_HOSTSTUBS_TASK_H

// Host-test stub for the FreeRTOS task API. Host tests are single threaded and
// drive the code under test directly, so no task ever gets created.

#include "FreeRTOS.h"

typedef void * TaskHandle_t;

static inline void vTaskDelay(unsigned long ticks) { (void)ticks; }

#endif // TEST_HOSTSTUBS_TASK_H
