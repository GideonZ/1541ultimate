#include "u2p.h"
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

#define MR     0x0232
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

int main()
{
	outbyte('@');
	DDR2_ENABLE    = 1;
	int i;
	for (i=0;i<1000;i++)
		;

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

	for (i=0;i<1000;i++)
		;
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
    	DDR2_READMODE = mode;
    	state = 0;
    	best_pos = -1;
    	best_length = 0;
    	for (phase = 0; phase < 160; phase ++) { // 2x 80 PLL phases: 720 degrees
    		good = 0;
    		for (rep = 0; rep < 7; rep ++) {
    			if (DDR2_TESTLOC0 == TESTVALUE1)
    				good++;
    			if (DDR2_TESTLOC1 == TESTVALUE2)
    				good++;
    		}
			DDR2_PLLPHASE = 0x35; // move read clock
//    		printf("%x", good);

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
		DDR2_PLLPHASE = 0x35; // move read clock
	}
	ioWrite8(UART_DATA, '@');
	ioWrite8(UART_DATA, hexchars[best_mode]);
	ioWrite8(UART_DATA, hexchars[final_pos >> 4]);
	ioWrite8(UART_DATA, hexchars[final_pos & 15]);
	ioWrite8(UART_DATA, '\r');
	ioWrite8(UART_DATA, '\n');

	uint32_t flash_addr = 0x80000;
	REMOTE_FLASHSEL_0;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;

	SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = W25Q_ContinuousArrayRead_LowFrequency;
    SPI_FLASH_DATA = (uint8_t)(flash_addr >> 16);
    SPI_FLASH_DATA = (uint8_t)(flash_addr >> 8);
    SPI_FLASH_DATA = (uint8_t)(flash_addr);

    uint32_t *dest   = (uint32_t *)SPI_FLASH_DATA_32;
    int      length  = (int)SPI_FLASH_DATA_32;
    uint32_t run_address = SPI_FLASH_DATA_32;
//    uint32_t version = SPI_FLASH_DATA_32;

    if(length != -1) {
        while(length > 0) {
            *(dest++) = SPI_FLASH_DATA_32;
            length -= 4;
        }
    	SPI_FLASH_CTRL = 0; // reset SPI chip select to idle
        puts("Running Tester.");
    	uint8_t buttons = ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS;
    	if ((buttons & ITU_BUTTON2) == 0) {  // right button not pressed
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

