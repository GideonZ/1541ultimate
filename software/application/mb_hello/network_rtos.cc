/*
 * network_rtos.cc
 *
 *  Created on: Mar 9, 2015
 *      Author: Gideon
 */

#include <stdio.h>
#include "init_function.h"
#include "FreeRTOS.h"
#include "task.h"
#include <errno.h>

int main (void)
{
	printf("Main function.\n");
	InitFunction::executeAll();

	vTaskSuspend(NULL);

	errno = 77;
	return 1;
}
