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
#define BOOT_MAGIC_LOCATION *((volatile uint32_t*)0x1FFFC)
#define BOOT_MAGIC_JUMPADDR *((volatile uint32_t*)0x1FFF8)
#define BOOT_MAGIC_VALUE    (0x1571BABE)

void jump_run(uint32_t a)
{
    void (*function)();
    uint32_t *dp = (uint32_t *)&function;
    *dp = a;
    function();
}

#define APPL "application\n"

const uint8_t hexchars[] = "0123456789ABCDEF";

void hexbyte(uint8_t val)
{
    outbyte(hexchars[(val >> 4)  & 15]);
    outbyte(hexchars[(val >> 0)  & 15]);
}

void hexword(uint32_t val)
{
    uint8_t *p = (uint8_t *)&val;
    hexbyte(p[3]);
    hexbyte(p[2]);
    hexbyte(p[1]);
    hexbyte(p[0]);
    outbyte(' ');
}

void my_puts(const char *str)
{
    while(*str) {
        outbyte(*(str++));
    }
}

#define MAX_APPL_SIZE 0x140000

int main()
{
    puts("Hello world!");

    uint32_t capabilities = getFpgaCapabilities();
    hexword(capabilities);

	if (capabilities & CAPAB_SIMULATION) {
		jump_run(0x30000);
	}

    if (capabilities > 1) {
        uint32_t flash_addr = 0x220000; // 2176K from start. FPGA image is (uncompressed) 2141K
        
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
                __asm__("nop");
                __asm__("nop");
            }
        }
    } else if(BOOT_MAGIC_LOCATION == BOOT_MAGIC_VALUE) {
        my_puts("Magic!\n");
        BOOT_MAGIC_LOCATION = 0;
        jump_run(BOOT_MAGIC_JUMPADDR);
    }

    my_puts("Empty\n");

    volatile uint32_t *some_mem = (uint32_t *)0x20000;

    while(1) {
        for (int i=0;i<16;i++) {
            hexword(some_mem[i]);
        }
        my_puts("\n");
        for (int i=0;i<16;i++) {
            some_mem[i] += i;
        }
        //some_mem += 16;
        __asm__("nop");
        __asm__("nop");
    }
    return 0;
#endif
}
