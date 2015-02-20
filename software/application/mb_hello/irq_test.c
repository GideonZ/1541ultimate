#include "small_printf.h"
#include "itu.h"

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
    
    ITU_IRQ_TIMER_LO  = 0x4B;
	ITU_IRQ_TIMER_HI  = 0x4C;
    ITU_IRQ_TIMER_EN  = 0x01;
    ITU_IRQ_ENABLE    = 0x01;

    while(1) {
        printf("IRQ_Count = %d\r", irq_count);
    }
    return 0;
}


void myISR(void)
{
    irq_count ++;
    ITU_IRQ_CLEAR = 0x01;
}
