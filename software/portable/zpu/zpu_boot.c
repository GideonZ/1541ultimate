#include "itu.h"

extern int _cpu_config;

int _use_syscall;
extern int _syscall(int *foo, int ID, ...);

extern int main(int argc, char **argv);

static char *args[]={"dummy.exe"};
unsigned int _memreg[4];
const unsigned int ZPU_ID=1;

extern void _init(void);
void _initIO(void);

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

static volatile int *TIMER;
volatile int *MHZ;
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

typedef void(*fptr)(void);

extern unsigned char __constructor_list, __end_of_constructors;
extern unsigned char __destructor_list, __end_of_destructors;

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

    pul = (unsigned long *)&__constructor_list;
    while(pul != (unsigned long *)&__end_of_constructors) {
        f = (fptr)*pul;
        f();
        pul++;
    }
        
    UART_DATA = 0x33;
    UART_DATA = 0x2e;

	t=main(1, args);

/*
    pul = (unsigned long *)&__destructor_list;
    while(pul != (unsigned long *)&__end_of_destructors) {
        f = (fptr)*pul;
        f();
        pul++;
    }
*/
	for (;;);
}

/*
void *sbrk(int inc)
{
	start_brk+=inc;
	return start_brk;
}
*/
