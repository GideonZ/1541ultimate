// Stand-in for the RTOS headers: just enough for subsys.h and the tape code. The behaviour
// (a task that is only created, a delay that is a scripted step) lives in tape_env.cc.
#ifndef TAPE_TESTS_FREERTOS_H
#define TAPE_TESTS_FREERTOS_H
#include <stdint.h>
#include <stddef.h>

typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void (*TaskFunction_t)(void *);

#define pdFALSE 0
#define pdTRUE  1
#define pdPASS  1
#define portMAX_DELAY 0xFFFFFFFFu
#define configMINIMAL_STACK_SIZE 256
#define PRIO_REALTIME 4
// A critical section of the target (portENTER_CRITICAL, system/itu.h): no interrupt and no task switch
// until it is left. The tests count them and do not run the interrupt or the task inside one.
namespace tapeenv { void safe_enter(); void safe_leave(); }
#define ENTER_SAFE_SECTION tapeenv::safe_enter()
#define LEAVE_SAFE_SECTION tapeenv::safe_leave()
#endif
