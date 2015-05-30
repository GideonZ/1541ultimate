#include "integer.h"
#include "flash.h"
#include "sd_card.h"
#include "disk.h"
#include "fat_fs.h"
#include "fatfile.h"
//#include "usb_scsi.h"
#include "versions.h"

extern "C" {
	#include "itu.h"
	#include "xmodem.h"
    #include "dump_hex.h"
    #include "small_printf.h"
}

#define APPLICATION_RUN_ADDRESS 0x1000000
#define APPLICATION_MAX_LENGTH  0x400000

BlockDevice  *blk;
Disk         *dsk;
Partition    *prt;
FATFS        *fs;
//UsbDevice	 *usbdev;

void (*function)();

void jump_run(void)
{
    uint32_t *dp = (uint32_t *)&function;
    *dp = APPLICATION_RUN_ADDRESS;
    function();
}

int init_fat(void)
{
    dsk   = new Disk(blk, 512);

    int res = dsk->Init();
    printf("Disk initialized. Returned: %d\n", res);

    if(res < 1) {
        delete dsk;
        delete blk;
        return -1;
    }    

    Partition *prt;
    prt = dsk->partition_list; // get first partition
    fs = new FATFS(prt);
    if(!(fs = FATFS :: test(prt))) {
        printf("Did not find FAT file system.\n");
        delete prt;
        delete dsk;
        delete blk;
        return -2;
    }
    fs->init();
    return 0;
}

int init_fat_on_sd(void)
{
    blk = new SdCard();
    return init_fat();
}

FRESULT try_loading(char *filename)
{
    FATFIL *file = new FATFIL(fs);
    uint16_t idx_retval;
    FRESULT res = file->open(filename, 0, FA_READ, &idx_retval);
    printf("File %s open result = %d.\n", filename, res);
    if(res != FR_OK) {
        return res;
    }
    uint32_t bytes_read = 0;
    res = file->read((void *)APPLICATION_RUN_ADDRESS, APPLICATION_MAX_LENGTH, &bytes_read);
    printf("Bytes read: %d (0x%6x)\n", bytes_read, bytes_read);
    
    file->close();
    delete file;

    if(bytes_read) {
        //execute
        jump_run();
        return FR_OK;
    }
    return FR_INVALID_OBJECT;
}

int try_flash(void)
{
    uint32_t length;
	Flash *flash = get_flash();
	t_flash_address image_addr;
    
    char version[12];
    
	flash->get_image_addresses(FLASH_ID_APPL, &image_addr);
    flash->read_dev_addr(image_addr.device_addr+0,  4, &length); // could come from image_addr, too, if we fix it in the interface
    flash->read_dev_addr(image_addr.device_addr+4, 12, version); // we should create a flash call for this on the interface
    
/*
    // write test
    flash->protect_disable();
    flash->write_page(0, (void *)0);
    flash->read_dev_addr(0, 0x200, (void *)0x90000);
    dump_hex((void *)0x90000, 0x200);
*/
        
    printf("Application length = %08x, version %s\n", length, version);
    if(length != 0xFFFFFFFF) {
        flash->read_dev_addr(image_addr.device_addr+16, length, (void *)APPLICATION_RUN_ADDRESS); // we should use flash->read_image here
        jump_run();
        return 1;
    }
    return 0; // fail
}

int try_xmodem(void)
{
    int st = xmodemReceive((uint8_t *)APPLICATION_RUN_ADDRESS, APPLICATION_MAX_LENGTH);
	if (st < 0) {
		printf ("Xmodem receive error: status: %d\n", st);
		return st;
	}
	printf ("Xmodem successfully received %d bytes\n", st);
    jump_run();
    return 1;
}

int main()
{
    printf("*** 1541 Ultimate-II - Loader - FPGA Version: %2x ***\n\n", getFpgaVersion());

    FRESULT res = FR_DISK_ERR;
    int file_system_err;
	bool skip_flash = false;
	file_system_err = init_fat_on_sd();

    if(!file_system_err) { // will return error code, 0 = ok
        res = try_loading("ultimate_" APPL_VERSION ".u2u");
        if(res!=FR_OK)
            res = try_loading("update.u2u");
        delete fs;
        delete prt;
        delete dsk;
        delete blk;
    } else {
	   printf("File system error: %d\n", file_system_err);
	}

    if(res != FR_OK) {
        if(skip_flash || (!try_flash())) { // will return 0 on fail
            if(try_xmodem() < 0) {
                printf("\nYou're dead!\n");
            }
        }
    } else {
        printf("Application successfully executed.\n");
    }
}
