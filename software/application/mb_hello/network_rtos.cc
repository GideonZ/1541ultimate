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

int main (void)
{
	printf("Main function.\n");
	InitFunction::executeAll();

	vTaskDelay(-1);
	return 1;
}
