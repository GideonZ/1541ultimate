#include "spiflash.h"
extern "C" {
    #include "itu.h"
}

#define BOOT2_RUN_ADDR 0x10000
#define APPL_RUN_ADDR  0x20000

void (*function)();

void jump_run(DWORD a)
{
    DWORD *dp = (DWORD *)&function;
    *dp = a;
    function();
}

int main(int argc, char **argv)
{
    UINT length;
    uart_write_buffer("**Primary Boot**\n\r", 18);
    flash.read_devaddr(FLASH_DEVADDR_BOOTAPP, 4, &length);    
    if(length != 0xFFFFFFFF) {
        flash.read_devaddr(FLASH_DEVADDR_BOOTAPP+16, length, (void *)BOOT2_RUN_ADDR);
        jump_run(BOOT2_RUN_ADDR);
        while(1);
    }
    uart_write_buffer("No bootloader, trying application.\n\r", 36);
    flash.read_devaddr(FLASH_DEVADDR_APPL, 4, &length);    
    if(length != 0xFFFFFFFF) {
        flash.read_devaddr(FLASH_DEVADDR_APPL+16, length, (void *)APPL_RUN_ADDR);
        jump_run(BOOT2_RUN_ADDR);
        while(1);
    }
    uart_write_buffer("Nothing to boot.\n\r", 18);
    while(1);
}
