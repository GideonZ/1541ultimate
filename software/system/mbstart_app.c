#include "small_printf.h"
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

extern char _heap[];
extern char _heap_end[];
void *sbrk(int inc);

//extern void _init(void);
void _initIO();
void __clear_bss();
void __copy_data();

void _premain() __attribute__ ((section(".text.start")));

typedef void(*fptr)(void);

void _premain()
{
	int t;
	unsigned long *pul, *end;
	fptr f;

    UART_DATA = 0x31;
    UART_DATA = 0x2e;

//    printf("Heap: %p-%p\n", _heap, _heap_end);
//	__copy_data();
//	_initIO();
    __clear_bss();

    UART_DATA = 0x32;
    UART_DATA = 0x2e;

    pul = (unsigned long *)&__constructor_list;
    end = (unsigned long *)&__end_of_constructors;
//    printf("\nconstructor list = (%p-%p)\n", pul, end);

    while(pul != end) {
        f = (fptr)*pul;
        printf("Con %p\n", f);
        f();
        pul++;
    }

    UART_DATA = 0x33;
    UART_DATA = 0x2e;

	t=main(0, 0);

    pul = (unsigned long *)&__end_of_destructors;
    do {
        pul--;
        if(pul < (unsigned long *)&__destructor_list)
        	break;
        f = (fptr)*pul;
        f();
    } while(1);
    
    puts("Done!");
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
/*
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>

int getpid(void)
{
	return 1;
}

int kill(int pid, int sig)
{
	errno = EINVAL;
	return -1;
}

int write(int file, char *ptr, int len)
{
	   int todo;

	   for (todo = 0; todo < len; todo++)
	   {
		   outbyte( *ptr++ );
	   }

	// Implement your write code here, this is used by puts and printf for example
	return len;
}

int close(int file)
{
	return -1;
}


int fstat(int file, struct stat *st)
{
	st->st_mode = S_IFCHR;
	return 0;
}

int isatty(int file)
{
	return 1;
}

int lseek(int file, int ptr, int dir)
{
	return 0;
}

int read(int file, char *ptr, int len)
{
	return 0;
}

int open(char *path, int flags, ...)
{
	// Pretend like we always fail
	return -1;
}

int wait(int *status)
{
	errno = ECHILD;
	return -1;
}

int unlink(char *name)
{
	errno = ENOENT;
	return -1;
}

int link(char *old, char *new)
{
	errno = EMLINK;
	return -1;
}

int fork(void)
{
	errno = EAGAIN;
	return -1;
}

int execve(char *name, char **argv, char **env)
{
	errno = ENOMEM;
	return -1;
}
*/
