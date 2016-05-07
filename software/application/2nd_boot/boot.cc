#include "integer.h"
#include "flash.h"
#include "sd_card.h"
#include "disk.h"
#include "ff2.h"
//#include "usb_scsi.h"
#include "versions.h"

extern "C" {
	#include "itu.h"
	#include "xmodem.h"
    #include "small_printf.h"
}

#define APPLICATION_RUN_ADDRESS 0x10000
#define APPLICATION_MAX_LENGTH  0x400000
#define UPDATER_RUN_ADDRESS 	0x1000000

BlockDevice  *blk;
Disk         *dsk;
Partition    *prt;

FATFS		 fs;

//UsbDevice	 *usbdev;

void (*function)();

void jump_run(uint32_t run_address)
{
    uint32_t *dp = (uint32_t *)&function;
    *dp = run_address;
    function();
}

#define BS_55AA             510
#define BS_FilSysType       54
#define BS_FilSysType32     82

bool test_fat(Partition *prt)
{
	uint32_t secsize;
	DRESULT status = prt->ioctl(GET_SECTOR_SIZE, &secsize);
	uint8_t *buf;
	if(status)
		return false;
	if(secsize > 4096)
		return false;
	if(secsize < 512)
		return false;

	buf = new uint8_t[secsize];

	DRESULT dr = prt->read(buf, 0, 1);
	if (dr != RES_OK) {	/* Load boot record */
		printf("FileSystemFAT::test failed, because reading sector failed: %d\n", dr);
		//delete buf;
		return false;
	}

	if (LD_WORD(&buf[BS_55AA]) != 0xAA55) {	/* Check record signature (always placed at offset 510 even if the sector size is >512) */
		//delete buf;
		return false;
	}
	if ((LD_DWORD(&buf[BS_FilSysType]) & 0xFFFFFF) == 0x544146)	{ /* Check "FAT" string */
		//delete buf;
		return true;
	}
	if ((LD_DWORD(&buf[BS_FilSysType32]) & 0xFFFFFF) == 0x544146) {
		//delete buf;
		return true;
	}
	//delete buf;
	return false;
}

int init_fat(void)
{
    dsk   = new Disk(blk, 512);

    int res = dsk->Init();
    printf("Disk initialized. Returned: %d\n", res);

    if(res < 1) {
        //delete dsk;
        //delete blk;
        return -1;
    }    

    Partition *prt;
    prt = dsk->partition_list; // get first partition

    if(!test_fat(prt)) {
        printf("Did not find FAT file system.\n");
        //delete prt;
        //delete dsk;
        //delete blk;
        return -2;
    }
    fs.drv = prt;
    fs_init_volume(&fs, 0);
    return 0;
}

int init_fat_on_sd(void)
{
    blk = new SdCard();
    return init_fat();
}

FRESULT try_loading(char *filename, uint32_t run_address)
{
    uint16_t dummy;
    FIL file;
    FRESULT res = fs_open(&fs, filename, FA_READ, &file);

    printf("File %s open result = %d.\n", filename, res);
    if(res != FR_OK) {
        return res;
    }
    uint32_t bytes_read = 0;
    res = f_read(&file, (void *)run_address, APPLICATION_MAX_LENGTH, &bytes_read);
    printf("Bytes read: %d (0x%6x)\n", bytes_read, bytes_read);
    
    f_close(&file);

    if(bytes_read) {
        //execute
        jump_run(run_address);
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
    
    printf("Application length = %08x, version %s\n", length, version);
    if(length != 0xFFFFFFFF) {
        flash->read_dev_addr(image_addr.device_addr+16, length, (void *)APPLICATION_RUN_ADDRESS); // we should use flash->read_image here

        jump_run(APPLICATION_RUN_ADDRESS);
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
    jump_run(APPLICATION_RUN_ADDRESS);
    return 1;
}

int main()
{
	volatile uint32_t *k = (volatile uint32_t *)0;

    custom_outbyte = 0;
	printf("*** 1541 Ultimate-II - Bootloader %s - FPGA Version: %2x - %08x %08x ***\n\n",
            BOOT_VERSION, getFpgaVersion(), *k++, *k++);

	k = (volatile uint32_t *)0;
	*k = 0xB0000000;

	if (getFpgaCapabilities() & CAPAB_SIMULATION) {
        ioWrite8(UART_DATA, '*');
        jump_run(APPLICATION_RUN_ADDRESS);
    }

    FRESULT res = FR_DISK_ERR;
    int file_system_err;
	bool skip_flash = false;

	file_system_err = init_fat_on_sd();

    if(!file_system_err) { // will return error code, 0 = ok
        res = try_loading("recover.u2u", UPDATER_RUN_ADDRESS);
        res = try_loading("ultimate.bin", APPLICATION_RUN_ADDRESS);
        //delete prt;
        //delete dsk;
        //delete blk;
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
        while(1)
        	;
    }
}
