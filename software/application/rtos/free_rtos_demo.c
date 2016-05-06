/* Scheduler includes. */
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "semphr.h"

#include "itu.h"
#include <stdio.h>

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
		vTaskDelay( 100 );

		ioWrite8(ITU_USB_BUSY, 0);
		vTaskDelay( 100 );
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
			vTaskDelay( 20 );
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
			vTaskDelay( 20 );
		} else {
			printf("#");
		}
	}
}

#include "dump_hex.h"

int main (void)
{
	/* When re-starting a debug session (rather than cold booting) we want
	to ensure the installed interrupt handlers do not execute until after the
	scheduler has been started. */
	puts("Hello FreeRTOS!");


	portDISABLE_INTERRUPTS();

	xTaskCreate( vLedFlash, "Flash my LED!", configMINIMAL_STACK_SIZE, NULL, mainLED_TASK_PRIORITY, NULL );
	xTaskCreate( vPrintSomething1, "Print Something 1", configMINIMAL_STACK_SIZE, NULL, mainLED_TASK_PRIORITY, NULL );
	xTaskCreate( vPrintSomething2, "Print Something 2", configMINIMAL_STACK_SIZE, NULL, mainLED_TASK_PRIORITY, NULL );

	xSemaphore = xSemaphoreCreateMutex();

	/* Finally start the scheduler. */
	vTaskStartScheduler();

	/* Should not get here as the processor is now under control of the
	scheduler! */

   	return 0;
}

int a = 0;

void irqHandler()
{
	a++;
	ioWrite8(ITU_IRQ_CLEAR, 0xFF);
	ioWrite8(ITU_USB_BUSY, a & 1);
}

int alt_irq_register( alt_u32 id, void* context, void (*handler) );

int mains(void)
{
    if ( -EINVAL == alt_irq_register( 1, 0x0, irqHandler ) )
	{
		/* Failed to install the Interrupt Handler. */
		asm( "break" );
	}
	else
	{
	    int timerDiv = (configCPU_CLOCK_HZ / configTICK_RATE_HZ) >> 8;
	    /* Configure SysTick to interrupt at the requested rate. */
	    ioWrite8(ITU_IRQ_TIMER_EN, 0);
	    ioWrite8(ITU_IRQ_DISABLE, 0xFF);
	    ioWrite8(ITU_IRQ_TIMER_HI, timerDiv >> 8);
	    ioWrite8(ITU_IRQ_TIMER_LO, timerDiv & 0xFF);
	    ioWrite8(ITU_IRQ_TIMER_EN, 1);
	    ioWrite8(ITU_IRQ_CLEAR, 0x01);
	    ioWrite8(ITU_IRQ_ENABLE, 0x01); // timer only : other modules shall enable their own interrupt
	    ioWrite8(UART_DATA, 0x35);
	}

    portENABLE_INTERRUPTS();

    while(1) {
    	printf("%6d\r", a);
    }
}

/*-----------------------------------------------------------*/
void vAssertCalled( char* fileName, uint16_t lineNo )
{
    printf("ASSERTION FAIL: %s:%d\n", fileName, lineNo);
    while(1)
        ;
}

