extern "C" {
	#include "small_printf.h"
	#include "itu.h"
	#include "xmodem.h"
}
#include "w25q_flash.h"
#include "at49_flash.h"

void (*function)();

void jump_run(DWORD a)
{
    DWORD *dp = (DWORD *)&function;
    *dp = a;
    function();
}

#define BOOT2_RUN_ADDR 0x10000
#define APPL_RUN_ADDR  0x10000
#define APPL_RUN_MK1   0x30000
/*
int main(void)
{
	puts("Hello, world!");

    BYTE *ram = (BYTE *)0x10000;

    while(1) {
    	int st = xmodemReceive((char *)ram, 1048576);
        if (st > 0) {
            puts("Receive done.");
            puts("Going to start...");
            wait_ms(2000);
            //jump_run(0x10000);
            UART_DATA = 0x2d;
            asm("bralid r15, 0x10000");
            asm("nop");
            puts("Application exit.");
        } else {
            puts("Nothing received.");
        }
    }    
    return 0;
}
*/

bool w25q_wait_ready(int time_out)
{
// TODO: FOR Microblaze or any faster CPU, we need to insert wait cycles here
    int i=time_out;
	bool ret = true;
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = W25Q_ReadStatusRegister1;
    while(SPI_FLASH_DATA & 0x01) {
        if(!(i--)) {
            puts("Flash timeout.");
            ret = false;
        }
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    return ret;
}

void uart_write_hex_long(DWORD hex)
{
    uart_write_hex(BYTE(hex >> 24));
    uart_write_hex(BYTE(hex >> 16));
    uart_write_hex(BYTE(hex >> 8));
    uart_write_hex(BYTE(hex));
}


int main(int argc, char **argv)
{
    if (getFpgaCapabilities() & CAPAB_SIMULATION) {
        UART_DATA = BYTE('*');
        jump_run(APPL_RUN_ADDR);
    }

    if (!(getFpgaCapabilities() & CAPAB_SPI_FLASH)) {
/*
        AT49_COMMAND1 = 0xAA;
        AT49_COMMAND2 = 0x55;
        AT49_COMMAND1 = 0x90;
    	BYTE manuf  = AT49_MEM_ARRAY(0);
    	BYTE dev_id = AT49_MEM_ARRAY(2);
        AT49_COMMAND1 = 0xAA;
        AT49_COMMAND2 = 0x55;
        AT49_COMMAND1 = 0xF0;
        uart_write_hex(manuf);
        uart_write_hex(dev_id);
*/
        jump_run(APPL_RUN_MK1);
    }

    puts("**Primary Boot**");

    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    SPI_FLASH_DATA = 0xFF;

    // check flash
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = 0x9F; // JEDEC Get ID
	BYTE manuf = SPI_FLASH_DATA;
	BYTE dev_id = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;

    DWORD read_boot2, read_appl;
    DWORD *dest;

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
    	BYTE status = SPI_FLASH_DATA;
        SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
        if ((status & 0x7C) != 0x34) { // on the spansion, this will protect 7/8 not 1/2
            SPI_FLASH_CTRL = 0;
        	SPI_FLASH_DATA = W25Q_WriteEnable;
            SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
        	SPI_FLASH_DATA = W25Q_WriteStatusRegister;
        	SPI_FLASH_DATA = 0x34; // 7/8 on spansion!!
        	SPI_FLASH_DATA = 0x00;
            SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
        	w25q_wait_ready(10000);
        	SPI_FLASH_DATA = W25Q_WriteDisable;
        }
    }

    int length;

/*
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA_32 = read_boot2;
    length = (int)SPI_FLASH_DATA_32;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;

    if(length != -1) {
        read_boot2 += 16;
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
        SPI_FLASH_DATA_32 = read_boot2;
        dest = (DWORD *)BOOT2_RUN_ADDR;
        while(length > 0) {
            *(dest++) = SPI_FLASH_DATA_32;
            length -= 4;
        }
//        uart_write_hex_long(*(DWORD *)BOOT2_RUN_ADDR);
//        uart_write_buffer("Running 2nd boot.\n\r", 19);
        jump_run(BOOT2_RUN_ADDR);
        while(1);
    }

*/
    puts("No bootloader, trying application.");

    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA_32 = read_appl;
    length = (int)SPI_FLASH_DATA_32;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;

    if(length != -1) {
        read_appl += 16;
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
        SPI_FLASH_DATA_32 = read_appl;
        dest = (DWORD *)APPL_RUN_ADDR;
        uart_write_hex_long((DWORD)dest);
        while(length > 0) {
            *(dest++) = SPI_FLASH_DATA_32;
            length -= 4;
        }
        UART_DATA = '-';
        uart_write_hex_long((DWORD)dest);
        jump_run(APPL_RUN_ADDR);
        while(1);
    }
    puts("Nothing to boot.");
    while(1);
}
