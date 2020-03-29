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

void ddr2_calibrate(void);

int main()
{
    DDR2_ENABLE    = 1;
#if RECOVERY
    uint8_t buttons = ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS;
    if ((buttons & ITU_BUTTON1) == 0) { // middle button not pressed
        REMOTE_FLASHSEL_1;
        REMOTE_FLASHSELCK_0;
        REMOTE_FLASHSELCK_1;
        REMOTE_RECONFIG = 0xBE;
    }
#endif

    ddr2_calibrate();

#if RECOVERY
	uint32_t flash_addr = 0x80000; // 512K from start. FPGA image is (compressed) ~330K
	REMOTE_FLASHSEL_0;
#else
	uint32_t flash_addr = 0xC0000; // 768K from start. FPGA image is (uncompressed) 745K
	REMOTE_FLASHSEL_1;
#endif
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

    puts(APPL);
    if(length != -1) {
        while(length > 0) {
            *(dest++) = SPI_FLASH_DATA_32;
            length -= 4;
        }
    	SPI_FLASH_CTRL = 0; // reset SPI chip select to idle
        puts("Running");
    	uint8_t buttons = ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS;
    	if ((buttons & ITU_BUTTON2) == 0) {  // right button not pressed
    		jump_run(run_address);
    	} else {
    		puts("Lock");
    	}
        while(1);
    }

    puts("Empty");
    while(1)
    	;
    return 0;
}
