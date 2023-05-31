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

void uart_write_hex_long(uint32_t hex)
{
    uart_write_hex(uint8_t(hex >> 24));
    uart_write_hex(uint8_t(hex >> 16));
    uart_write_hex(uint8_t(hex >> 8));
    uart_write_hex(uint8_t(hex));
}

int main(int argc, char **argv)
{
    custom_outbyte = 0;
    puts("**Primary Boot 3.3**");
    uint32_t capab = getFpgaCapabilities();
    uart_write_hex_long(capab);
    if (capab & CAPAB_SIMULATION) {
        ioWrite8(UART_DATA, '*');
        jump_run(BOOT2_RUN_ADDR);
    }
    ioWrite8(UART_DATA, '$');

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
        read_appl  = 0x030BE000;

        // protect flash the Atmel way
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    	SPI_FLASH_DATA_32 = 0x3D2A7FA9;
        SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    } else {
        flash_type = 1; // assume Winbond or Spansion
        read_boot2 = 0x03054000;
        read_appl  = 0x03062000;

        // Protection during boot is not required, as the bits are non-volatile.
        // They are supposed to be set in the updater.
        // Make sure CSn is high before we start
        SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
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
