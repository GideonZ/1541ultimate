/*
 * network_rtos.cc
 *
 *  Created on: Mar 9, 2015
 *      Author: Gideon
 */

/* Scheduler includes. */
extern "C" {
	#include "small_printf.h"
	#include "FreeRTOS.h"
	#include "FreeRTOSConfig.h"
	#include "task.h"
	#include "semphr.h"
	#include "itu.h"
	#include "small_printf.h"
}

#include "init_function.h"

void main_loop(void);

#define MAIN_LOOP_TASK_PRIORITY		( tskIDLE_PRIORITY + 2 )

SemaphoreHandle_t xSemaphore;

void run_main_loop(void *a)
{
	puts("Executing init functions.");
	InitFunction :: executeAll();
	puts("Starting main loop.");
	main_loop();
}

int main (void)
{
	/* When re-starting a debug session (rather than cold booting) we want
	to ensure the installed interrupt handlers do not execute until after the
	scheduler has been started. */
	puts("Hello FreeRTOS!");

	portDISABLE_INTERRUPTS();

	xTaskCreate( run_main_loop, "Main Event Loop", configMINIMAL_STACK_SIZE, NULL, MAIN_LOOP_TASK_PRIORITY, NULL );
	//xTaskCreate( vPrintSomething1, "Print Something 1", configMINIMAL_STACK_SIZE, NULL, mainLED_TASK_PRIORITY, NULL );
	//xTaskCreate( vPrintSomething2, "Print Something 2", configMINIMAL_STACK_SIZE, NULL, mainLED_TASK_PRIORITY + 1, NULL );

	xSemaphore = xSemaphoreCreateMutex();

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Should not get here as the processor is now under control of the
	scheduler! */

   	return 0;
}
