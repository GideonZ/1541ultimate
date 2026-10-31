#ifndef SOURCE_TEST_FREERTOS_H
#define SOURCE_TEST_FREERTOS_H
#include <assert.h>
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) do { assert(!*(m)); ++*(m); } while (0)
#define portEXIT_CRITICAL(m) do { assert(*(m)==1); --*(m); } while (0)
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
#endif
