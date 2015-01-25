#include "small_printf.h"

int irq_count = 0;

void myISR(void) __attribute__ ((interrupt_handler));

int main()
{
    puts("Hello IRQs!");
    
    // store interrupt handler in the right trap, for simulation this should work.
    unsigned int pointer = (unsigned int)&myISR;
    unsigned int *vector = (unsigned int *)0x10;
    vector[0] = (0xB0000000 | (pointer >> 16));
    vector[1] = (0xB8080000 | (pointer & 0xFFFF));

    // enable interrupts
    __asm__ ("msrset r0, 0x02");
    
    while(1) {
        printf("IRQ_Count = %d\n", irq_count * 1000);
    }
    return 0;
}


void myISR(void)
{
    irq_count ++;
}
