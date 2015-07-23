#include "small_printf.h"
#include "itu.h"
#include "xmodem.h"

void (*function)();

void jump_run(uint32_t a)
{
    uint32_t *dp = (uint32_t *)&function;
    *dp = a;
    function();
}

int main(void)
{
	puts("Hello, world!");

    uint8_t *ram = (uint8_t *)0x10000;

    while(1) {
    	int st = xmodemReceive(ram, 1048576);
        if (st > 0) {
            puts("Receive done.");
            //jump_run(0x10000);
            ioWrite8(UART_DATA, 0x2d);
            __asm__("bralid r15, 0x10000");
            __asm__("nop");
            puts("Application exit.");
        } else {
            puts("Nothing received.");
        }
    }    
    return 0;
}
