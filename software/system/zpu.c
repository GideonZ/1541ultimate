#include "itu.h"

int small_printf(const char *fmt, ...);

extern int _cpu_config;

/*
int _use_syscall;
extern int _syscall(int *foo, int ID, ...);
*/

extern int main(int argc, char **argv);

static char *args[]={"dummy.exe"};
unsigned int _memreg[4];
const unsigned int ZPU_ID=1;

extern unsigned char __constructor_list, __end_of_constructors;
extern unsigned char __destructor_list, __end_of_destructors;

//extern void _init(void);
void _initIO(void);
void __clear_bss(void);
void __copy_data(void);
void __construct(void);

void _premain() __attribute__ ((section(".mysection")));

typedef void(*fptr)(void);

void _premain()
{
	int t;
	unsigned long *pul;
	fptr f;

    UART_DATA = 0x31;
    UART_DATA = 0x2e;

//	__copy_data();
//	_initIO();
    __clear_bss();

    UART_DATA = 0x32;
    UART_DATA = 0x2e;

//    small_printf("\nCalling constructors...\n");

    pul = (unsigned long *)&__constructor_list;
    while(pul != (unsigned long *)&__end_of_constructors) {
//        small_printf("Constructor %p...\n", *pul);
        f = (fptr)*pul;
        f();
        pul++;
    }
        
    UART_DATA = 0x33;
    UART_DATA = 0x2e;

//    small_printf("\nStarting Main...\n");

	t=main(1, args);

    pul = (unsigned long *)&__end_of_destructors;
    do {
        pul--;
        if(pul < (unsigned long *)&__destructor_list)
        	break;
        small_printf("Destructor: %p...\n", *pul);
        f = (fptr)*pul;
        f();
    } while(1);

    small_printf("Done!\n");
//	exit(t);
//  for (;;);
}

void __construct(void)
{
}


extern unsigned char __bss_start__, __bss_end__;

void __clear_bss(void)
{
	unsigned int *cptr;
	cptr = (unsigned int*)&__bss_start__;
	do {
        *cptr = 0;
        cptr++;
    } while(cptr < (unsigned int *)(&__bss_end__));
}

extern unsigned char __ram_start,__data_start,__data_end;
/*
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
			return val;
		}
	}
}

/* 
 * Output one character to the serial port 
 * 
 * 
 */
//void outbyte(int c);


void outbyte(int c)
{
	// Wait for space in FIFO
	while (UART_FLAGS & UART_TxFifoFull);
	UART_DATA = c;
}


/*
void *sbrk(int inc)
{
	start_brk+=inc;
	return start_brk;
}
*/
