#include "flash.h"
#include "at45_flash.h"

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
	Flash *flash = get_flash();
	t_flash_address image_addr;
		
	flash->protect_enable();

    UINT length;
    uart_write_buffer("**Primary Boot**\n\r", 18);
	flash->protect_enable();
	flash->get_image_addresses(FLASH_ID_BOOTAPP, &image_addr);
    flash->read_dev_addr(image_addr.device_addr, 4, &length);    
    if(length != 0xFFFFFFFF) {
        flash->read_dev_addr(image_addr.device_addr+16, length, (void *)BOOT2_RUN_ADDR);
        jump_run(BOOT2_RUN_ADDR);
        while(1);
    }
    uart_write_buffer("No bootloader, trying application.\n\r", 36);
	flash->get_image_addresses(FLASH_ID_APPL, &image_addr);
    flash->read_dev_addr(image_addr.device_addr, 4, &length);    
    if(length != 0xFFFFFFFF) {
        flash->read_dev_addr(image_addr.device_addr+16, length, (void *)APPL_RUN_ADDR);
        jump_run(APPL_RUN_ADDR);
        while(1);
    }
    uart_write_buffer("Nothing to boot.\n\r", 18);
    while(1);
}
/*

int main(int argc, char **argv)
{
	t_flash_address image_addr;
		
    UINT length;
    uart_write_buffer("**Primary Boot**\n\r", 18);
	at45_flash.get_image_addresses(FLASH_ID_BOOTAPP, &image_addr);
    at45_flash.read_dev_addr(image_addr.device_addr, 4, &length);    
    if(length != 0xFFFFFFFF) {
        at45_flash.read_dev_addr(image_addr.device_addr+16, length, (void *)BOOT2_RUN_ADDR);
        jump_run(BOOT2_RUN_ADDR);
        while(1);
    }
    uart_write_buffer("No bootloader, trying application.\n\r", 36);
	at45_flash.get_image_addresses(FLASH_ID_APPL, &image_addr);
    at45_flash.read_dev_addr(image_addr.device_addr, 4, &length);    
    if(length != 0xFFFFFFFF) {
        at45_flash.read_dev_addr(image_addr.device_addr+16, length, (void *)APPL_RUN_ADDR);
        jump_run(APPL_RUN_ADDR);
        while(1);
    }
    uart_write_buffer("Nothing to boot.\n\r", 18);
    while(1);
}
*/
