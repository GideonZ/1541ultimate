#include "u2p.h"
#include "itu.h"
#include <stdio.h>

//#define DDR2_TESTLOC0  (*(volatile uint32_t *)(0x0630))
//#define DDR2_TESTLOC1  (*(volatile uint32_t *)(0x0634))
#define JUMP_LOCATION  (*(volatile uint32_t *)(0x20000780))

void jump_run(uint32_t a)
{
	void (*function)();
	uint32_t *dp = (uint32_t *)&function;
    *dp = a;
    function();
}

void outbyte(int c)
{
	// Wait for space in FIFO
	while (ioRead8(UART_FLAGS) & UART_TxFifoFull);
	ioWrite8(UART_DATA, c);
}

void ddr2_calibrate();

int main()
{
    ddr2_calibrate();
    while(1) {
		puts("Waiting for JTAG download");
		JUMP_LOCATION = 0;
		while(!JUMP_LOCATION)
			;
		outbyte('>');
		jump_run(JUMP_LOCATION);
		outbyte('<');
	}
    return 0;
}
