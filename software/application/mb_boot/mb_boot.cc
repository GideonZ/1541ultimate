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
    uart_write_buffer("Application exit.\n", 18);
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

void send_addr(uint32_t addr)
{
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = 0x03;
    SPI_FLASH_DATA = addr >> 16;
    SPI_FLASH_DATA = addr >> 8;
    SPI_FLASH_DATA = (uint8_t)addr;
}

int get_length(uint32_t addr)
{
    int length;
    uint32_t dummy; 

    send_addr(addr);
    length  = ((int)SPI_FLASH_DATA) << 24;
    length |= ((int)SPI_FLASH_DATA) << 16;
    length |= ((int)SPI_FLASH_DATA) << 8;
    length |= ((int)SPI_FLASH_DATA);
    dummy = SPI_FLASH_DATA_32;
    dummy = SPI_FLASH_DATA_32;
    dummy = SPI_FLASH_DATA_32;
    //uart_write_hex_long(length);
    return length;
}

uint32_t *load(int length)
{
    uint32_t *dest = (uint32_t *)BOOT2_RUN_ADDR;
    while(length > 0) {
        *(dest++) = SPI_FLASH_DATA_32;
        length -= 4;
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    //uart_write_hex_long((uint32_t)dest);
    return dest;
}

void go(uint32_t boot2, uint32_t appl)
{
    int length;

    length = get_length(boot2);
    if(length != -1) {
        load(length);
        uart_write_buffer("Running 2nd boot.\n", 18);
        jump_run(BOOT2_RUN_ADDR);
        while(1);
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    uart_write_buffer("No bootloader.\n", 15);

    length = get_length(appl);
    if(length != -1) {
        load(length);
        // outbyte('-');
        jump_run(APPL_RUN_ADDR);
        while(1);
    }
    uart_write_buffer("Nothing to boot.\n", 17);
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    while(1);
}

int main(int argc, char **argv)
{
    custom_outbyte = 0;

    uart_write_buffer("**Primary Boot 3.4**\n", 20);
    uint32_t capab = getFpgaCapabilities();
    // uart_write_hex_long(capab);
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

    int flash_type = 0;
    if(manuf == 0x1F) { // Atmel 
        // protect flash the Atmel way
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    	SPI_FLASH_DATA = 0x3D;
        SPI_FLASH_DATA = 0x2A;
        SPI_FLASH_DATA = 0x7F;
        SPI_FLASH_DATA = 0xA9;
        SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    } else {
        flash_type = 1; // assume Winbond or Spansion

        // Protection during boot is not required, as the bits are non-volatile.
        // They are supposed to be set in the updater.
        // Make sure CSn is high before we start
    }

    if (flash_type) {
        go(0x54000, 0x62000);
    } else {
        go(0xA2800, 0xBE000);
    }
}
