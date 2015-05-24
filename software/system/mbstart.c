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

    UART_DATA = 0x31;
    UART_DATA = 0x2e;

	t=main(0, 0);

}


/*
 * Wait indefinitely for input byte
 */

int inbyte()
{
	int val;
	for (;;)
	{
		val=UART_FLAGS;
		if ((val&UART_RxDataAv)!=0)
		{
			val = UART_DATA;
			UART_GET = 0;
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
void outbyte(int c)
{
	// Wait for space in FIFO
	while (UART_FLAGS & UART_TxFifoFull);
	UART_DATA = (uint8_t)c;
}

void restart(void)
{
	uint32_t *src = (uint32_t *)0x1C00000;
	uint32_t *dst = (uint32_t *)0x10000;
	int size = (int)(*(src++));
	while(size--) {
		*(dst++) = *(src++);
	}
    //puts("Restarting....");
    __asm__("bralid r15, 0x10000");
    __asm__("nop");
}

/*
void *sbrk(int inc)
{
	start_brk+=inc;
	return start_brk;
}
*/
