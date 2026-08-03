#ifndef TEST_HOSTSTUBS_QUEUE_H
#define TEST_HOSTSTUBS_QUEUE_H

// Host-test stub for the FreeRTOS queue API. The command-interface tests drive
// executeCommand() directly rather than posting to a queue, so only the type
// needs to exist.

#include "FreeRTOS.h"

typedef void * QueueHandle_t;

#endif // TEST_HOSTSTUBS_QUEUE_H
