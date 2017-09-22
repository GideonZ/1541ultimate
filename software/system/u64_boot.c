#include "u2p.h"
#include "u64.h"
#include "itu.h"
#include <stdio.h>

#define W25Q_ContinuousArrayRead_LowFrequency       0x03

#define SPI_FLASH_DATA     *((volatile uint8_t *)(FLASH_BASE + 0x00))
#define SPI_FLASH_DATA_32  *((volatile uint32_t*)(FLASH_BASE + 0x00))
#define SPI_FLASH_CTRL     *((volatile uint8_t *)(FLASH_BASE + 0x08))

#define SPI_FORCE_SS 0x01
#define SPI_LEVEL_SS 0x02

#define DDR2_TESTLOC0  (*(volatile uint32_t *)(0x0000))
#define DDR2_TESTLOC1  (*(volatile uint32_t *)(0x0004))

#define MR     0x0232 // 00 | 0 0 1 (write recovery=2) 0 (dll reset) | 0 (testmode) 0 1 1 (CL=3) | 0 (seq burst) 0 1 0 (BL=4)
#define MR8    0x0233 // 00 | 0 0 1 (write recovery=2) 0 (dll reset) | 0 (testmode) 0 1 1 (CL=3) | 0 (seq burst) 0 1 1 (BL=8)
#define EMR    0x4440 // 01 0 0 0 1 (no DQSn) 000 (no OCD) 1 (150ohm) 000 (no AL) 0 (150 ohm) 0 (full drive) 0 (dll used)
#define EMROCD 0x47C0 // 01 0 0 0 1 (no DQSn) 111 (do OCD) 1 (150ohm) 000 (no AL) 0 (150 ohm) 0 (full drive) 0 (dll used)
#define EMR2   0x8000 // no extended refresh
#define EMR3   0xC000 // all bits reserved
#define DLLRST 0x0100 // MR DLL RESET

#define TESTVALUE1  0x55AA6699
#define TESTVALUE2  0x12345678

const uint8_t hexchars[] = "0123456789ABCDEF";

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

void hexword(uint32_t data)
{
	for(int i=0;i<8;i++) {
		outbyte(hexchars[data >> 28]);
		data <<= 4;
	}
	outbyte('\n');
}

#define CLK_DIVIDER 9
#define VCO_PHASE_STEPS 8
#define PHASE_STEPS (CLK_DIVIDER * VCO_PHASE_STEPS)

int main()
{
	outbyte('@');
	DDR2_ENABLE    = 1;

	DDR2_ADDR_LOW  = 0x00;
    DDR2_ADDR_HIGH = 0x04;
    DDR2_COMMAND   = 2; // Precharge all

    DDR2_ADDR_LOW  = (EMR2 & 0xFF);
    DDR2_ADDR_HIGH = (EMR2 >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = (EMR3 & 0xFF);
    DDR2_ADDR_HIGH = (EMR3 >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = (EMR & 0xFF);
    DDR2_ADDR_HIGH = (EMR >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = (DLLRST & 0xFF);
    DDR2_ADDR_HIGH = (DLLRST >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = 0x00;
    DDR2_ADDR_HIGH = 0x04;
    DDR2_COMMAND   = 2; // Precharge all

    DDR2_COMMAND   = 1; // Refresh
    DDR2_COMMAND   = 1; // Refresh
    DDR2_COMMAND   = 1; // Refresh
    DDR2_COMMAND   = 1; // Refresh

    DDR2_ADDR_LOW  = (MR & 0xFF);
    DDR2_ADDR_HIGH = (MR >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = (EMROCD & 0xFF);
    DDR2_ADDR_HIGH = (EMROCD >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = (EMR & 0xFF);
    DDR2_ADDR_HIGH = (EMR >> 8);
    DDR2_COMMAND   = 0; // write MR

	for (int i=0;i<1000;i++) {

	}
	//    alt_putstr("DDR2 Initialized!\n\r"); // delay!

	DDR2_TESTLOC0 = TESTVALUE1;
    DDR2_TESTLOC1 = TESTVALUE2;

    int mode, phase, rep, good;
    int last_begin, best_pos, best_length;
    int state;
    int best_mode = -1;
    int final_pos = 0;
    int best_overall = -1;

    for (mode = 0; mode < 4; mode ++) {
    	// outbyte('\n');
    	DDR2_READMODE = mode;
    	state = 0;
    	best_pos = -1;
    	best_length = 0;
    	for (phase = 0; phase < (2 * PHASE_STEPS); phase ++) { // 720 degrees
    		good = 0;
//    		hexword(DDR2_TESTLOC0);
    		for (rep = 0; rep < 7; rep ++) {
    			if (DDR2_TESTLOC0 == TESTVALUE1)
    				good++;
    			if (DDR2_TESTLOC1 == TESTVALUE2)
    				good++;
    		}
			DDR2_PLLPHASE = 0x33; // move read clock
//			outbyte(hexchars[good]);

    		if ((state == 0) && (good >= 13)) {
    			last_begin = phase;
    			state = 1;
    		} else if ((state == 1) && (good < 13)) {
//    			printf("(%d:%d:%d)", last_begin, phase, phase - last_begin);
    			state = 0;
    			if ((phase - last_begin) > best_length) {
    				best_length = phase - last_begin;
    				best_pos = last_begin + (best_length >> 1);
    			}
    		}
    	}
    	if (best_length > best_overall) {
    		best_overall = best_length;
    		best_mode = mode;
    		final_pos = best_pos;
    	}
    }

    //printf("Chosen: Mode = %d, Pos = %d. Window = %d ps\n\r", best_mode, final_pos, 100 * best_overall);
    DDR2_READMODE = best_mode;
	for (phase = 0; phase < final_pos; phase ++) {
		DDR2_PLLPHASE = 0x33; // move read clock
	}
	outbyte('\n');
	outbyte(hexchars[best_mode & 15]);
	outbyte(hexchars[final_pos >> 4]);
	outbyte(hexchars[final_pos & 15]);
	outbyte('\r');
	outbyte('\n');

	uint16_t *mem16 = (uint16_t *)0;
	for(int i=0;i<65536;i++) {
		mem16[i] = (uint16_t)i;
	}
	int errors = 0;
	for(int i=0;i<65536;i++) {
		if (mem16[i] != (uint16_t)i)
			errors++;
	}
	if (errors) {
		puts("RAM error.");
		while(1);
	}

	uint32_t flash_addr = 0x290000;

	SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = W25Q_ContinuousArrayRead_LowFrequency;
    SPI_FLASH_DATA = (uint8_t)(flash_addr >> 16);
    SPI_FLASH_DATA = (uint8_t)(flash_addr >> 8);
    SPI_FLASH_DATA = (uint8_t)(flash_addr);

    uint32_t *dest   = (uint32_t *)SPI_FLASH_DATA_32;
    int      length  = (int)SPI_FLASH_DATA_32;
    uint32_t run_address = SPI_FLASH_DATA_32;

    if(length != -1) {
        while(length > 0) {
            *(dest++) = SPI_FLASH_DATA_32;
            length -= 4;
        }
    	SPI_FLASH_CTRL = 0; // reset SPI chip select to idle
    	if (U64_RESTORE_REG == 0) {  // restore button not pressed
            puts("Running U64.");
    		jump_run(run_address);
    	} else {
    		puts("Lock");
    	}
        while(1);
    }

    puts("Flash Empty");
    while(1)
    	;
    return 0;
}
