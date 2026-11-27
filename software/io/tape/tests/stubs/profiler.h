// The profiler register is a raw store to the trace block; on the host it is a variable.
#ifndef TAPE_TESTS_PROFILER_H
#define TAPE_TESTS_PROFILER_H
#include <stdint.h>
extern volatile uint8_t tape_test_profiler_sub;
#define PROFILER_SUB tape_test_profiler_sub
#endif
