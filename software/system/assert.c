/*
 * assert.c
 *
 *  Created on: Apr 27, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"

// Defined by the application that owns the remote log (ultimate.cc). Weak,
// because assert.c is linked into applications that have no syslog at all,
// and there a null pointer here is the right answer.
extern void syslog_flush(void) __attribute__((weak));

void print_tasks(void)
{
	static char buffer[8192];
	vTaskList(buffer);
	puts(buffer);
}

/*-----------------------------------------------------------*/
void vAssertCalled(const char* fileName, uint16_t lineNo )
{
    // Printed and forwarded before interrupts go off. Once this task is in a
    // critical section the syslog task never runs again, so anything written
    // after that point stays in the forwarding buffer and the one message
    // worth having is the one that never leaves the machine. The flush is a
    // no-op when nothing is listening.
    printf("ASSERTION FAIL: %s:%d\n", fileName, lineNo);
    print_tasks();
    if (syslog_flush) {
        syslog_flush();
    }

	portENTER_CRITICAL();
	while(1)
		;
}


