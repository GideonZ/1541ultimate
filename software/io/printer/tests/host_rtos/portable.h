#include "FreeRTOS.h"
#include <stddef.h>
inline size_t &host_rtos_free_heap(void) { static size_t v = (size_t) 64 << 20; return v; }
static inline size_t xPortGetFreeHeapSize(void) { return host_rtos_free_heap(); }
