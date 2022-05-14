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

void vPortSetupTimerInterrupt( void )
{
	uint32_t freq = 50000000;
	freq >>= 8;
	freq /= 200;
	freq -= 1;

	ioWrite8(ITU_IRQ_HIGH_EN, 0);
    ioWrite8(ITU_IRQ_TIMER_EN, 0);
	ioWrite8(ITU_IRQ_DISABLE, 0xFF);
	ioWrite8(ITU_IRQ_CLEAR, 0xFF);
	ioWrite8(ITU_IRQ_TIMER_HI, (uint8_t)(freq >> 8));
	ioWrite8(ITU_IRQ_TIMER_LO, (uint8_t)(freq & 0xFF));
	ioWrite8(ITU_IRQ_TIMER_EN, 1);
	ioWrite8(ITU_IRQ_ENABLE, 0x01); // timer only : other modules shall enable their own interrupt
	ioWrite8(ITU_IRQ_GLOBAL, 0x01); // Enable interrupts globally
	ioWrite8(UART_DATA, 0x35);
}

void ituIrqHandler(void *context)
{
	static uint8_t pending;

	/* Which interrupts are pending? */
	pending = ioRead8(ITU_IRQ_ACTIVE);
	ioWrite8(ITU_IRQ_CLEAR, pending);

	BaseType_t do_switch = pdFALSE;

	if (pending & 0x01) {
		do_switch |= xTaskIncrementTick();
	}

	if (do_switch != pdFALSE) {
		vTaskSwitchContext();
	}
}
