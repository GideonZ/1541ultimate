#include "itu.h"

/*
int _use_syscall;
extern int _syscall(int *foo, int ID, ...);
*/

extern int main(int argc, char **argv);

//static char *args[]={"dummy.exe"};
//unsigned int _memreg[4];

extern unsigned char __constructor_list, __end_of_constructors;
extern unsigned char __destructor_list, __end_of_destructors;

//extern void _init(void);
void _initIO() __attribute__ ((section(".mysection")));
void __clear_bss() __attribute__ ((section(".mysection")));
void __copy_data() __attribute__ ((section(".mysection")));
void __construct() __attribute__ ((section(".mysection")));
void __cache_to_sdram() __attribute__ ((section(".mysection")));

void _premain() __attribute__ ((section(".mysection")));

typedef void(*fptr)(void);

void _premain()
{
	int t;

	t=main(0, 0);
}

void _exit(void) {
	while(1)
		;
}

/*
 * Wait indefinitely for input byte
 */

int inbyte()
{
	int val;
	for (;;)
	{
		val=ioRead8(UART_FLAGS);
		if ((val&UART_RxDataAv)!=0)
		{
			val = UART_DATA;
			ioWrite8(UART_GET, 0);
			break;
		}
	}
	return val;
}

/* 
 * Output one character to the serial port 
 * 
 * 
 */
/*
void outbyte(int c)
{
	// Wait for space in FIFO
	while (ioRead8(UART_FLAGS) & UART_TxFifoFull);
	ioWrite8(UART_DATA, (uint8_t)c);
}
*/

void restart(void)
{
	uint32_t *src = (uint32_t *)0x1C00000;
	int size = (int)(*(src++));
	uint32_t *dst = (uint32_t *)(*(src++));
	uint32_t *jump = dst;

	while(size--) {
		*(dst++) = *(src++);
	}
	dst = (uint32_t *)0xF000;
	for (int i=0;i<1022;i++) {
		*(dst++) = 0x80000000; // NOP instruction
	}
	*(dst++) = 0xb6030008; // RTSD R3, 8
	*(dst++) = 0x80000000; // NOP instruction
	// Note that the data cache has now been completely overwritten
	// The instruction cache will now pick up the code
	// this works because we have a write through cache.
	// puts("Restarting....");
    __asm__("bralid r3, 0xF000"); // execute the whole bunch of NOPs and return
    __asm__("nop");
    __asm__("nop");
    __asm__("bra %0" : : "r"(jump)); // now that the instruction cache got flushed, jump to the newly loaded code
    __asm__("nop");
    //  We never get here
}



/*
void *sbrk(int inc)
{
	start_brk+=inc;
	return start_brk;
}
*/
