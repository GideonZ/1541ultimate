#include <stdio.h>

int main(int argc, char **argv)
{
    __asm__("li s1, 0x800");
    __asm__("csrs mie, s1");    // enable external interrupts
    __asm__("csrs mstatus, 8"); // enable global interrupt flag

    printf("Hello world! :-)\n");

    __asm__("nop");
    __asm__("nop");
    __asm__("nop");
    __asm__("nop");
    __asm__("nop");
//    __asm__("ecall");
    return 1;
}
