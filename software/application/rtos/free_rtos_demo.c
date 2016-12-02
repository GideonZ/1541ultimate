/* Scheduler includes. */
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "semphr.h"

#include "itu.h"
#include "small_printf.h"

#define mainLED_TASK_PRIORITY		( tskIDLE_PRIORITY + 2 )

/*
 * Create all the demo tasks - then start the scheduler.
 */
SemaphoreHandle_t xSemaphore;

void vLedFlash()
{
	for( ;; )
	{
		ioWrite8(ITU_USB_BUSY, 1);
		vTaskDelay( 50 );

		ioWrite8(ITU_USB_BUSY, 0);
		vTaskDelay( 50 );
	}
}

void vPrintSomething1()
{
	int count = 0;
	for( ;; )
	{
		if (xSemaphoreTake(xSemaphore, 100 )) {
			printf("Task 1 %6x\n", count++);
			xSemaphoreGive(xSemaphore);
			vTaskDelay( 1 );
		} else {
			printf("@");
		}
	}
}

void vPrintSomething2()
{
	int count = 0;
	for( ;; )
	{
		if (xSemaphoreTake(xSemaphore, 100 )) {
			printf("Task 2 %6x\n", count++);
			xSemaphoreGive(xSemaphore);
			vTaskDelay( 1 );
		} else {
			printf("#");
		}
	}
}

int main (void)
{
	/* When re-starting a debug session (rather than cold booting) we want

	to ensure the installed interrupt handlers do not execute until after the
	scheduler has been started. */
	puts("Hello FreeRTOS!");

	portDISABLE_INTERRUPTS();

	xTaskCreate( vLedFlash, "Flash my LED!", configMINIMAL_STACK_SIZE, NULL, mainLED_TASK_PRIORITY, NULL );
	xTaskCreate( vPrintSomething1, "Print Something 1", configMINIMAL_STACK_SIZE, NULL, mainLED_TASK_PRIORITY, NULL );
	xTaskCreate( vPrintSomething2, "Print Something 2", configMINIMAL_STACK_SIZE, NULL, mainLED_TASK_PRIORITY + 1, NULL );

	xSemaphore = xSemaphoreCreateMutex();

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Should not get here as the processor is now under control of the
	scheduler! */

   	return 0;
}

