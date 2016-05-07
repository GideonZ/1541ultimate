extern "C" {
	#include "small_printf.h"
	#include "itu.h"
}
#include "w25q_flash.h"
#include "at49_flash.h"


void (*function)();

void jump_run(uint32_t a)
{
	uint32_t *dp = (uint32_t *)&function;
    *dp = a;
    function();
    puts("Application exit.");
    while(1)
    	;
}

#define BOOT2_RUN_ADDR 0x1000
#define APPL_RUN_ADDR  0x10000

bool w25q_wait_ready(int time_out)
{
	bool ret = true;
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = W25Q_ReadStatusRegister1;
    uint16_t start = getMsTimer();
	uint16_t now;
    while(SPI_FLASH_DATA & 0x01) {
    	now = getMsTimer();
    	if ((now - start) > time_out) {
    		puts("Flash timeout.");
            ret = false;
            break;
        }
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    return ret;
}


int main(int argc, char **argv)
{
    if (getFpgaCapabilities() & CAPAB_SIMULATION) {
        ioWrite8(UART_DATA, '*');
        jump_run(BOOT2_RUN_ADDR);
    }
    ioWrite8(UART_DATA, '$');
    custom_outbyte = 0;
    puts("**Primary Boot 3.1**");

	SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    SPI_FLASH_DATA = 0xFF;

    // check flash
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = 0x9F; // JEDEC Get ID
	uint8_t manuf = SPI_FLASH_DATA;
	uint8_t dev_id = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;

    uint32_t read_boot2, read_appl;
    uint32_t *dest;

    int flash_type = 0;
    if(manuf == 0x1F) { // Atmel
        read_boot2 = 0x030A2800;
        read_appl  = 0x03200000;

        // protect flash the Atmel way
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    	SPI_FLASH_DATA_32 = 0x3D2A7FA9;
        SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    } else {
        flash_type = 1; // assume Winbond or Spansion
        read_boot2 = 0x03054000;
        read_appl  = 0x03100000;

        // protect flash the Winbond way
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    	SPI_FLASH_DATA = W25Q_ReadStatusRegister1;
    	uint8_t status = SPI_FLASH_DATA;
        SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
        if ((status & 0x7C) != 0x34) { // on the spansion, this will protect 7/8 not 1/2
            SPI_FLASH_CTRL = 0;
        	SPI_FLASH_DATA = W25Q_WriteEnable;
            SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
        	SPI_FLASH_DATA = W25Q_WriteStatusRegister;
        	SPI_FLASH_DATA = 0x34; // 7/8 on spansion!!
        	SPI_FLASH_DATA = 0x00;
            SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
        	w25q_wait_ready(50); // datasheet winbond: 15 ms max
        	SPI_FLASH_DATA = W25Q_WriteDisable;
        }
    }

    int length;

    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA_32 = read_boot2;
    length = (int)SPI_FLASH_DATA_32;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;

    if(length != -1) {
        read_boot2 += 16;
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
        SPI_FLASH_DATA_32 = read_boot2;
        dest = (uint32_t *)BOOT2_RUN_ADDR;
        while(length > 0) {
            *(dest++) = SPI_FLASH_DATA_32;
            length -= 4;
        }
        //uart_write_hex_long(*(uint32_t *)BOOT2_RUN_ADDR);
        uart_write_buffer("Running 2nd boot.\n\r", 19);
        jump_run(BOOT2_RUN_ADDR);
        while(1);
    }

    puts("No bootloader.");

    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA_32 = read_appl;
    length = (int)SPI_FLASH_DATA_32;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;

    if(length != -1) {
        read_appl += 16;
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
        SPI_FLASH_DATA_32 = read_appl;
        dest = (uint32_t *)APPL_RUN_ADDR;
        //uart_write_hex_long((uint32_t)dest);
        while(length > 0) {
            *(dest++) = SPI_FLASH_DATA_32;
            length -= 4;
        }
        ioWrite8(UART_DATA, '-');
        //uart_write_hex_long((uint32_t)dest);
        jump_run(APPL_RUN_ADDR);
        while(1);
    }
    puts("Nothing to boot.");
    while(1);
}
