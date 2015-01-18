#include "small_printf.h"
#include "xmodem.h"
#include "itu.h"

void (*function)();

void jump_run(DWORD a)
{
    DWORD *dp = (DWORD *)&function;
    *dp = a;
    function();
}

void dump_ram(BYTE *ram)
{
    int i,j;    
    for(i=0;i<10;i++) {
        for(j=0;j<32;j++) {
            uart_write_hex(*(ram++));
        }
        outbyte('\r');
        outbyte('\n');
    }
}
    
int main(void)
{
	puts("Hello, world!");

    BYTE *ram = (BYTE *)0x10000;

    while(1) {
    	int st = xmodemReceive((char *)ram, 1048576);
        if (st > 0) {
            puts("Receive done.");
        	dump_ram(ram);
            puts("Going to start...");
            wait_ms(2000);
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
