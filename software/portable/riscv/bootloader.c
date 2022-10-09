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

#define APPL "application\n"

void ddr2_calibrate(void);
void hexword(uint32_t);
void my_puts(const char *str)
{
    while(*str) {
        outbyte(*(str++));
    }
}

#define MAX_APPL_SIZE 0x140000

int main()
{
    uint32_t capabilities = getFpgaCapabilities();
	if (capabilities & CAPAB_SIMULATION) {
		jump_run(0x30000);
	}

    outbyte('#');
    ddr2_calibrate();

    if (capabilities) {
        uint32_t flash_addr = 0xA0000; // 640K from start. FPGA image is (uncompressed) 571K
        
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
        SPI_FLASH_DATA = W25Q_ContinuousArrayRead_LowFrequency;
        SPI_FLASH_DATA = (uint8_t)(flash_addr >> 16);
        SPI_FLASH_DATA = (uint8_t)(flash_addr >> 8);
        SPI_FLASH_DATA = (uint8_t)(flash_addr);

        uint32_t *dest   = (uint32_t *)SPI_FLASH_DATA_32;
        int      length  = (int)SPI_FLASH_DATA_32;
        uint32_t run_address = SPI_FLASH_DATA_32;

        hexword((uint32_t)dest);
        hexword((uint32_t)length);
        hexword(run_address);

    #if NO_BOOT == 1
        my_puts("Waiting for JTAG download!\n\n");
        while(1) {
            ioWrite8(ITU_SD_BUSY, 1);
            ioWrite8(ITU_SD_BUSY, 0);
        }
    //    uint32_t version = SPI_FLASH_DATA_32;
    #else
        my_puts(APPL);

        if(length != -1) {
            while ((length > 0) && (length < MAX_APPL_SIZE)) {
                *(dest++) = SPI_FLASH_DATA_32;
                length -= 4;
            }
            SPI_FLASH_CTRL = 0; // reset SPI chip select to idle
            my_puts("Running\n");
            uint8_t buttons = ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS;
            if ((buttons & ITU_BUTTON2) == 0) {  // right button not pressed
                jump_run(run_address);
            } else {
                my_puts("Lock\n");
            }
            while(1) {
                ioWrite8(ITU_SD_BUSY, 1);
                ioWrite8(ITU_SD_BUSY, 0);
            }
        }
    }

    my_puts("Empty\n");
    while(1) {
        ioWrite8(ITU_SD_BUSY, 1);
        ioWrite8(ITU_SD_BUSY, 0);
    }
    return 0;
#endif
}
