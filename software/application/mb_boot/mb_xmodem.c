#include "small_printf.h"
#include "itu.h"
#include "xmodem.h"

void (*function)();

void jump_run(DWORD a)
{
    DWORD *dp = (DWORD *)&function;
    *dp = a;
    function();
}

int main(void)
{
	puts("Hello, world!");

    BYTE *ram = (BYTE *)0x10000;

    while(1) {
    	int st = xmodemReceive(ram, 1048576);
        if (st > 0) {
            puts("Receive done.");
            //jump_run(0x10000);
            UART_DATA = 0x2d;
            asm("bralid r15, 0x10000");
            asm("nop");
            puts("Application exit.");
        } else {
            puts("Nothing received.");
        }
    }    
    return 0;
}

void restart(void)
{
	DWORD *src = (DWORD *)0x1C00000;
	DWORD *dst = (DWORD *)0x10000;
	int size = (int)(*(src++));
	while(size--) {
		*(dst++) = *(src++);
	}
    puts("Restarting....");
    asm("bralid r15, 0x10000");
    asm("nop");
}
