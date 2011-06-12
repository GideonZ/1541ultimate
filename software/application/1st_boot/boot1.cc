#include "w25q_flash.h"
#include "at49_flash.h"

extern "C" {
    #include "itu.h"
}

#define BOOT2_RUN_ADDR 0x10000
#define APPL_RUN_ADDR  0x20000
#define APPL_RUN_MK1   0x30000

void (*function)();

void jump_run(DWORD a)
{
    DWORD *dp = (DWORD *)&function;
    *dp = a;
    function();
}

bool w25q_wait_ready(int time_out)
{
// TODO: FOR Microblaze or any faster CPU, we need to insert wait cycles here
    int i=time_out;
	bool ret = true;
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = W25Q_ReadStatusRegister1;
    while(SPI_FLASH_DATA & 0x01) {
        if(!(i--)) {
            uart_write_buffer("Flash timeout.\n", 16);
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
    if (CAPABILITIES & CAPAB_SIMULATION) {
        UART_DATA = BYTE('*');
        jump_run(APPL_RUN_ADDR);
    }

    if (!(CAPABILITIES & CAPAB_SPI_FLASH)) {
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

    uart_write_buffer("**Primary Boot**\n\r", 18);

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
        flash_type = 1; // assume Winbond
        read_boot2 = 0x03054000;
        read_appl  = 0x03100000;

        // protect flash the Winbond way
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    	SPI_FLASH_DATA = W25Q_ReadStatusRegister1;
    	BYTE status = SPI_FLASH_DATA;
        SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
        if ((status & 0x7C) != 0x34) {
            SPI_FLASH_CTRL = 0;
        	SPI_FLASH_DATA = W25Q_WriteEnable;
            SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
        	SPI_FLASH_DATA = W25Q_WriteStatusRegister;
        	SPI_FLASH_DATA = 0x34;
        	SPI_FLASH_DATA = 0x00;
            SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
        	w25q_wait_ready(10000);
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

    uart_write_buffer("No bootloader, trying application.\n\r", 36);

    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA_32 = read_appl;
    length = (int)SPI_FLASH_DATA_32;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
   
    if(length != -1) {
        read_appl += 16;
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
        SPI_FLASH_DATA_32 = read_appl;
        dest = (DWORD *)APPL_RUN_ADDR;
        while(length > 0) {
            *(dest++) = SPI_FLASH_DATA_32;
            length -= 4;
        }
        jump_run(APPL_RUN_ADDR);
        while(1);
    }
    uart_write_buffer("Nothing to boot.\n\r", 18);
    while(1);
}
