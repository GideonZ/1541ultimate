/* 
 * "Small Hello World" example. 
 * 
 * This example prints 'Hello from Nios II' to the STDOUT stream. It runs on
 * the Nios II 'standard', 'full_featured', 'fast', and 'low_cost' example 
 * designs. It requires a STDOUT  device in your system's hardware. 
 *
 * The purpose of this example is to demonstrate the smallest possible Hello 
 * World application, using the Nios II HAL library.  The memory footprint
 * of this hosted application is ~332 bytes by default using the standard 
 * reference design.  For a more fully featured Hello World application
 * example, see the example titled "Hello World".
 *
 * The memory footprint of this example has been reduced by making the
 * following changes to the normal "Hello World" example.
 * Check in the Nios II Software Developers Manual for a more complete 
 * description.
 * 
 * In the SW Application project (small_hello_world):
 *
 *  - In the C/C++ Build page
 * 
 *    - Set the Optimization Level to -Os
 * 
 * In System Library project (small_hello_world_syslib):
 *  - In the C/C++ Build page
 * 
 *    - Set the Optimization Level to -Os
 * 
 *    - Define the preprocessor option ALT_NO_INSTRUCTION_EMULATION 
 *      This removes software exception handling, which means that you cannot 
 *      run code compiled for Nios II cpu with a hardware multiplier on a core 
 *      without a the multiply unit. Check the Nios II Software Developers 
 *      Manual for more details.
 *
 *  - In the System Library page:
 *    - Set Periodic system timer and Timestamp timer to none
 *      This prevents the automatic inclusion of the timer driver.
 *
 *    - Set Max file descriptors to 4
 *      This reduces the size of the file handle pool.
 *
 *    - Check Main function does not exit
 *    - Uncheck Clean exit (flush buffers)
 *      This removes the unneeded call to exit when main returns, since it
 *      won't.
 *
 *    - Check Don't use C++
 *      This builds without the C++ support code.
 *
 *    - Check Small C library
 *      This uses a reduced functionality C library, which lacks  
 *      support for buffering, file IO, floating point and getch(), etc. 
 *      Check the Nios II Software Developers Manual for a complete list.
 *
 *    - Check Reduced device drivers
 *      This uses reduced functionality drivers if they're available. For the
 *      standard design this means you get polled UART and JTAG UART drivers,
 *      no support for the LCD driver and you lose the ability to program 
 *      CFI compliant flash devices.
 *
 *    - Check Access device drivers directly
 *      This bypasses the device file system to access device drivers directly.
 *      This eliminates the space required for the device file system services.
 *      It also provides a HAL version of libc services that access the drivers
 *      directly, further reducing space. Only a limited number of libc
 *      functions are available in this configuration.
 *
 *    - Use ALT versions of stdio routines:
 *
 *           Function                  Description
 *        ===============  =====================================
 *        alt_printf       Only supports %s, %x, and %c ( < 1 Kbyte)
 *        alt_putstr       Smaller overhead than puts with direct drivers
 *                         Note this function doesn't add a newline.
 *        alt_putchar      Smaller overhead than putchar with direct drivers
 *        alt_getchar      Smaller overhead than getchar with direct drivers
 *
 */

#include "u2p.h"
#include "itu.h"
#include "iomap.h"
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
/*
uint8_t hexchar(int a)
{
	if (a < 10) {
		return (uint8_t)(a + '0');
	}
	return (uint8_t)(a + 'a' - 10);
}
*/

void jump_run(uint32_t a)
{
	void (*function)();
	uint32_t *dp = (uint32_t *)&function;
    *dp = a;
    function();
}

#if RECOVERY
#define APPL "bootloader"
#else
#define APPL "application"
#endif

int main()
{ 
	DDR2_ENABLE    = 1;
	int i;
	for (i=0;i<1000;i++)
		;

#if RECOVERY
	uint8_t buttons = ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS;
	if ((buttons & ITU_BUTTON1) == 0) { // middle button not pressed
		REMOTE_FLASHSEL_1;
	    REMOTE_FLASHSELCK_0;
	    REMOTE_FLASHSELCK_1;
	    REMOTE_RECONFIG = 0xBE;
	}
#endif

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
//    	printf(": %d\n\r", best_pos);
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

/*
	for (phase = 0; phase < 160; phase ++) {
		uint8_t measure = DDR2_MEASURE;
		if ((measure & 0x0F) > (measure >> 4)) {
			printf("+");
		} else if ((measure & 0x0F) < (measure >> 4)) {
			printf("-");
		} else {
			printf("0");
		}
		DDR2_PLLPHASE = 0x36; // move measure clock
	}
	printf("\n\r");
*/
#if RECOVERY
	uint32_t flash_addr = 0x80000; // 512K from start. FPGA image is (compressed) ~330K
#else
	uint32_t flash_addr = 0xC0000; // 768K from start. FPGA image is (uncompressed) 745K
#endif

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
        puts("Running " APPL ".");
        jump_run(run_address);
        while(1);
    }

    puts("No " APPL ".");

    return 0;
}

