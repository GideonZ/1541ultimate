#ifndef TEST_HOSTSTUBS_SEMPHR_H
#define TEST_HOSTSTUBS_SEMPHR_H

// Host-test stub for the FreeRTOS semaphore API.
//
// Host tests are single threaded, so the mutexes that guard the firmware's
// shared state have nothing to protect. Take/Give are no-ops that always
// succeed, which keeps the locking call sites in the firmware unchanged.

#include "FreeRTOS.h"

typedef void * SemaphoreHandle_t;

#ifndef pdTRUE
#define pdTRUE  1
#define pdFALSE 0
#endif

#ifndef portMAX_DELAY
#define portMAX_DELAY 0xFFFFFFFFUL
#endif

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    static int mutex;
    return &mutex;
}

static inline SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return xSemaphoreCreateMutex();
}

static inline int xSemaphoreTake(SemaphoreHandle_t handle, unsigned long ticks)
{
    (void)handle;
    (void)ticks;
    return pdTRUE;
}

static inline int xSemaphoreGive(SemaphoreHandle_t handle)
{
    (void)handle;
    return pdTRUE;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t handle)
{
    (void)handle;
}

#endif // TEST_HOSTSTUBS_SEMPHR_H
