#ifndef PROFILER_H
#define PROFILER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "integer.h"
#include "iomap.h"
#include <stddef.h>

#define PROFILER_SUB   *((volatile uint8_t *)(TRACE_BASE + 0x04))
#define PROFILER_TASK  *((volatile uint8_t *)(TRACE_BASE + 0x05))
#define PROFILER_STOP  *((volatile uint8_t *)(TRACE_BASE + 0x06))
#define PROFILER_START *((volatile uint8_t *)(TRACE_BASE + 0x07))
#define PROFILER_ADDR *((volatile uint32_t *)(TRACE_BASE + 0x00))

void *profiled_memcpy(void *str1, const void *str2, size_t n);

#ifdef __cplusplus
}
#endif
#endif
