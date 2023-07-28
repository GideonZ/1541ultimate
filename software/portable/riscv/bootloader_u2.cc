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

void hex16(uint16_t val, const char *postfix)
{
    uint8_t *p = (uint8_t *)&val;
    uart_write_hex(p[1]);
    uart_write_hex(p[0]);
    while(*postfix) {
        outbyte(*(postfix++));
    }
}

#define TEST_WORD_COUNT 65536
#define RAM_TEST_REPORT 16

int ram_test(void)
{
    int errors = 0;

    uint16_t *mem16 = (uint16_t *)0x10000;
    const uint16_t modifier = 0x55AA;
    uint32_t i;

    for(i=0;i<TEST_WORD_COUNT;i++) {
        mem16[i] = (uint16_t)(i ^ modifier);
    }

    for(i=0;i<TEST_WORD_COUNT;i++) {
        uint16_t expected = (uint16_t)(i ^ modifier);
        uint16_t read = mem16[i];
        if (read != expected) {
            if (errors < RAM_TEST_REPORT) {
                hex16(errors,":");
                hex16(expected, " != ");
                hex16(read, "\n");
            }
            errors++;
        }
    }

    if (errors) {
        puts("RAM error.");
        return 0;
    }
    puts("RAM OK!!");
    return 1;
}

int main(int argc, char **argv)
{
    custom_outbyte = 0;

    puts("**Primary Boot 3.4**");
    uint32_t capab = getFpgaCapabilities();
    uart_write_hex_long(capab);
    if (capab & CAPAB_SIMULATION) {
        ioWrite8(UART_DATA, '*');
        jump_run(BOOT2_RUN_ADDR);
    }
    ioWrite8(UART_DATA, '$');

	SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    SPI_FLASH_DATA = 0xFF;

    ram_test();

    // check flash
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = 0x9F; // JEDEC Get ID
	uint8_t manuf = SPI_FLASH_DATA;
	uint8_t dev_id = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;

    uint8_t read_boot2[4];
    uint8_t read_appl[4];
    uint32_t *dest;

    // outbyte('(');
    // uart_write_hex(manuf);
    // outbyte(',');
    // uart_write_hex(dev_id);
    // outbyte(')');

    read_boot2[0] = 3;
    read_appl[0] = 3;
    read_boot2[3] = 0;
    read_appl[3] = 0;

    int flash_type = 0;
    if(manuf == 0x1F) { // Atmel 
        read_boot2[1] = 0x0a; // 0A2800
        read_boot2[2] = 0x28;
        read_appl[1] = 0x0B; // 0BE000
        read_appl[2] = 0xE0;

        // protect flash the Atmel way
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    	SPI_FLASH_DATA = 0x3D;
        SPI_FLASH_DATA = 0x2A;
        SPI_FLASH_DATA = 0x7F;
        SPI_FLASH_DATA = 0xA9;
        SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    } else {
        flash_type = 1; // assume Winbond or Spansion
        read_boot2[1] = 0x05; // 054000
        read_boot2[2] = 0x40;
        read_appl[1] = 0x06; // 062000
        read_appl[2] = 0x20;

        // Protection during boot is not required, as the bits are non-volatile.
        // They are supposed to be set in the updater.
        // Make sure CSn is high before we start
        SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    }

    int length;
    uint32_t dummy; 

    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = read_boot2[0];
    SPI_FLASH_DATA = read_boot2[1];
    SPI_FLASH_DATA = read_boot2[2];
    SPI_FLASH_DATA = read_boot2[3];
    length  = ((int)SPI_FLASH_DATA) << 24;
    length |= ((int)SPI_FLASH_DATA) << 16;
    length |= ((int)SPI_FLASH_DATA) << 8;
    length |= ((int)SPI_FLASH_DATA);
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;

    if(length != -1) {
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
        SPI_FLASH_DATA = read_boot2[0];
        SPI_FLASH_DATA = read_boot2[1];
        SPI_FLASH_DATA = read_boot2[2];
        SPI_FLASH_DATA = read_boot2[3];
        // skip the first 16 bytes
        dummy = SPI_FLASH_DATA_32;
        dummy = SPI_FLASH_DATA_32;
        dummy = SPI_FLASH_DATA_32;
        dummy = SPI_FLASH_DATA_32;
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
    SPI_FLASH_DATA = read_appl[0];
    SPI_FLASH_DATA = read_appl[1];
    SPI_FLASH_DATA = read_appl[2];
    SPI_FLASH_DATA = read_appl[3];
    length  = ((int)SPI_FLASH_DATA) << 24;
    length |= ((int)SPI_FLASH_DATA) << 16;
    length |= ((int)SPI_FLASH_DATA) << 8;
    length |= ((int)SPI_FLASH_DATA);
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    uart_write_hex_long((uint32_t)length);
    outbyte(':');

    if(length != -1) {
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
        SPI_FLASH_DATA = read_appl[0];
        SPI_FLASH_DATA = read_appl[1];
        SPI_FLASH_DATA = read_appl[2];
        SPI_FLASH_DATA = read_appl[3];
        // skip the first 16 bytes
        dummy = SPI_FLASH_DATA_32;
        dummy = SPI_FLASH_DATA_32;
        dummy = SPI_FLASH_DATA_32;
        dummy = SPI_FLASH_DATA_32;
        dest = (uint32_t *)APPL_RUN_ADDR;
        uart_write_hex_long((uint32_t)dest);
        while(length > 0) {
            *(dest++) = SPI_FLASH_DATA_32;
            length -= 4;
        }
        outbyte('-');
        uart_write_hex_long((uint32_t)dest);
        SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;

        jump_run(APPL_RUN_ADDR);
        while(1);
    }
    puts("Nothing to boot.");
    while(1);
}
