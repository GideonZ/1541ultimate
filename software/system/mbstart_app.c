#include "small_printf.h"
#include "itu.h"
#include <stdlib.h>

/*
int _use_syscall;
extern int _syscall(int *foo, int ID, ...);
*/

extern int main(int argc, char **argv);

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
//        printf("Destr %p\n", f);
        f();
    } while(1);
}

void _do_ctors(void)
{
	unsigned long *pul, *end;
	fptr f;

	pul = (unsigned long *)&__constructor_list;
    end = (unsigned long *)&__end_of_constructors;
//    printf("\nconstructor list = (%p-%p)\n", pul, end);

    while(pul != end) {
        f = (fptr)*pul;
//        printf("Con %p\n", f);
        f();
        pul++;
    }
}

void _exit()
{
    puts("Done!");
	while(1);
}

void _premain()
{
	int t;

	__clear_bss();

    _do_ctors();

    atexit(_do_dtors);
    t=main(0, 0);
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


void outbyte(int c)
{
	// Wait for space in FIFO
	while (ioRead8(UART_FLAGS) & UART_TxFifoFull);
	ioWrite8(UART_DATA, c);
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

