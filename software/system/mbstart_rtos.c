#include "small_printf.h"
#include "itu.h"
#include <stdlib.h>

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"

extern int main(int argc, char **argv);
void start_rtos (void);

extern unsigned char __constructor_list, __end_of_constructors;
extern unsigned char __destructor_list, __end_of_destructors;

extern char _heap[];
extern char _heap_end[];
void *sbrk(int inc);

//extern void _init(void);
void __clear_bss();
//void __copy_data();

void _premain() __attribute__ ((section(".text.start")));

typedef void(*fptr)(void);

void _do_ctors(void)
{
	unsigned long *pul, *end;
	fptr f;

	pul = (unsigned long *)&__constructor_list;
    end = (unsigned long *)&__end_of_constructors;
//    printf("\nconstructor list = (%p-%p)\n", pul, end);

    while(pul != end) {
        f = (fptr)*pul;
        printf("Con %p\n", f);
        f();
        pul++;
    }
}

void _do_dtors(void)
{
	unsigned long *pul;
	fptr f;

	pul = (unsigned long *)&__end_of_destructors;

    do {
        pul--;
        if(pul < (unsigned long *)&__destructor_list)
        	break;
        f = (fptr)*pul;
        printf("Destr %p\n", f);
        f();
    } while(1);
}

void _exit()
{
    puts("Done!");
	while(1);
}

void _premain()
{
    ioWrite8(UART_DATA, 0x31);
    ioWrite8(UART_DATA, 0x2e);

    __clear_bss();

    ioWrite8(UART_DATA, 0x32);
    ioWrite8(UART_DATA, 0x2e);

    atexit(_do_dtors);

    start_rtos();
}

void _construct_and_go()
{
	int t;

	_do_ctors();

    ioWrite8(UART_DATA, 0x33);
    ioWrite8(UART_DATA, 0x2e);

    t=main(0, 0);

    // TODO: Kill all tasks?
    exit(0);
}

extern char __bss_start__[], __bss_end__[];

void __clear_bss(void)
{
    //printf("Clear BSS: %p-%p\n", __bss_start__, __bss_end__);
	unsigned int *cptr;
	cptr = (unsigned int*)__bss_start__;
	do {
        *cptr = 0;
        cptr++;
    } while(cptr < (unsigned int *)(__bss_end__));
}

/*
extern unsigned char __ram_start,__data_start,__data_end;
extern unsigned char ___jcr_start,___jcr_begin,___jcr_end;

*/

/*
void __copy_data(void)
{
	unsigned int *cptr;
	cptr = (unsigned int*)&__ram_start;
	unsigned int *dptr;
	dptr = (unsigned int*)&__data_start;

	do {
		*(dptr++) = *(cptr++);
	} while (dptr<(unsigned int*)(&__data_end));

	cptr = (unsigned int*)&___jcr_start;
	dptr = (unsigned int*)&___jcr_begin;

	while (dptr<(unsigned int*)(&___jcr_end)) {
		*dptr = *cptr;
		cptr++,dptr++;
	}
}
*/

/* 
 * Output one character to the serial port 
 * 
 * 
 */
//void outbyte(int c);

void start_rtos (void)
{
	/* When re-starting a debug session (rather than cold booting) we want
	to ensure the installed interrupt handlers do not execute until after the
	scheduler has been started. */
	portDISABLE_INTERRUPTS();

	xTaskCreate( _construct_and_go, "U-II Main", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL );

	// Finally start the scheduler.
	vTaskStartScheduler();

	// Should not get here as the processor is now under control of the
	// scheduler!
}

void (*custom_outbyte)(int c) = 0;

void outbyte(int c)
{
	if (custom_outbyte) {
		custom_outbyte(c);
	} else {
		// Wait for space in FIFO
		while (ioRead8(UART_FLAGS) & UART_TxFifoFull);
		ioWrite8(UART_DATA, c);
	}
}


void *sbrk(int inc)
{
    static int b = (int)_heap;
    void *result = (void *)-1;
    if ((b + inc) < (int)_heap_end) {
        result = (void *)b;
        //printf("sbrk called with %6x. b = %p returning %p\n", inc, b, result);
        b += inc;
    } else {
        printf("Sbrk called with %6x. FAILED\n", inc);
    }
    return result;
}

void restart(void)
{
	uint32_t *src = (uint32_t *)0x1C00000;
	uint32_t *dst = (uint32_t *)0x10000;
	int size = (int)(*(src++));
	while(size--) {
		*(dst++) = *(src++);
	}
    puts("Restarting....");
    __asm__("bralid r15, 0x10000");
    __asm__("nop");
}


int *__errno()
{
	return &(_impure_ptr->_errno);
}

/*
void __malloc_lock()
{
	portENTER_CRITICAL();
}

void __malloc_unlock()
{
	portEXIT_CRITICAL();
}
*/

/*
void __cxa_guard_acquire(void)
{
}

void __cxa_guard_release(void)
{
}
*/

