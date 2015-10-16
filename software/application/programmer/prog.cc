#include "integer.h"
#include "sd_card.h"
#include "disk.h"
#include "ff2.h"
#include "versions.h"

extern "C" {
	#include "itu.h"
    #include "small_printf.h"
}

#include "dump_hex.h"

#define APPLICATION_RUN_ADDRESS 0x10000
#define APPLICATION_MAX_LENGTH  0x400000
#define UPDATER_RUN_ADDRESS 	0x1000000

BlockDevice  *blk;
Disk         *dsk;
Partition    *prt;

FATFS		 fs;

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

    dump_hex((void *)0, 64);

    if(bytes_read) {
        //execute
        jump_run(run_address);
        return FR_OK;
    }
    return FR_INVALID_OBJECT;
}


int main()
{
    printf("*** 1541 Ultimate-II - Bootloader %s - FPGA Version: %2x ***\n\n",
            BOOT_VERSION, getFpgaVersion());

    FRESULT res = FR_DISK_ERR;
    int file_system_err;

	file_system_err = init_fat_on_sd();

    if(!file_system_err) { // will return error code, 0 = ok
        res = try_loading("update.u2u", UPDATER_RUN_ADDRESS);
    } else {
	   printf("File system error: %d\n", file_system_err);
	}

	while(1)
		;
}
