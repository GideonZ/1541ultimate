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


