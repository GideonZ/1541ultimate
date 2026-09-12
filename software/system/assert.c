/*
 * assert.c
 *
 *  Created on: Apr 27, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"

void print_tasks(void)
{
	static char buffer[8192];
	vTaskList(buffer);
	puts(buffer);
}

/*-----------------------------------------------------------*/
void vAssertCalled(const char* fileName, uint16_t lineNo )
{
	portENTER_CRITICAL();
    printf("ASSERTION FAIL: %s:%d\n", fileName, lineNo);
	print_tasks();
	while(1)
		;
}

/*-----------------------------------------------------------*/
/* Called by the kernel at a context switch when the task being switched out has run
 * past the end of its stack (configCHECK_FOR_STACK_OVERFLOW). Whatever lies next to
 * that stack is already overwritten, so the task cannot be resumed; name it and stop,
 * so the log ends at the cause rather than at a later, unrelated failure. */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
	portENTER_CRITICAL();
	printf("STACK OVERFLOW in task '%s'\n", pcTaskName ? pcTaskName : "?");
	while(1)
		;
}

/*-----------------------------------------------------------*/
static void report_append(char *buf, int size, int *len, const char *fmt, ...)
{
	if (*len >= size - 1) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf + *len, size - *len, fmt, ap);
	va_end(ap);
	if (n > 0) {
		*len += n;
	}
	if (*len > size - 1) {
		*len = size - 1;
	}
}

/* Writes the heap figures and the stack head room of every task into buf, and
 * returns the length written. The head room is the least stack a task has had left
 * since it started, which is what shows whether a task came close to overrunning. */
int rtos_resource_report(char *buf, int size)
{
	int len = 0;
	if (!buf || (size < 1)) {
		return 0;
	}
	buf[0] = 0;

	report_append(buf, size, &len, "Heap free:        %u bytes\n", (unsigned)xPortGetFreeHeapSize());
	report_append(buf, size, &len, "Heap lowest free: %u bytes\n", (unsigned)xPortGetMinimumEverFreeHeapSize());
	report_append(buf, size, &len, "Heap size:        %u bytes\n\n", (unsigned)configTOTAL_HEAP_SIZE);

	UBaseType_t count = uxTaskGetNumberOfTasks();
	TaskStatus_t *tasks = (TaskStatus_t *)pvPortMalloc(count * sizeof(TaskStatus_t));
	if (!tasks) {
		return len;
	}
	count = uxTaskGetSystemState(tasks, count, NULL);
	report_append(buf, size, &len, "Task             Least stack free\n");
	for (UBaseType_t i = 0; i < count; i++) {
		report_append(buf, size, &len, "%-16s %u bytes\n", tasks[i].pcTaskName,
				(unsigned)(tasks[i].usStackHighWaterMark * sizeof(StackType_t)));
	}
	vPortFree(tasks);
	return len;
}


