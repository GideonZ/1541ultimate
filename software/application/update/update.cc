#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "versions.h"

extern "C" {
	#include "itu.h"
}
#include "c64.h"
#include "flash.h"
#include "screen.h"
#include "keyboard.h"
#include "sd_card.h"
#include "disk.h"
#include "fat_fs.h"
#include "fatfile.h"
#include "config.h"
#include "rtc.h"
#include "small_printf.h"
#include "userinterface.h"

extern BYTE _binary_ultimate_bin_start;
extern BYTE _binary_ultimate_bin_end;
extern BYTE _binary_1st_boot_700_bin_start;
extern BYTE _binary_1st_boot_700_bin_end;
extern BYTE _binary_2nd_boot_bin_start;
extern BYTE _binary_2nd_boot_bin_end;

Screen *screen;
UserInterface *user_interface;
Flash *flash;

/*
BlockDevice  *blk;
Disk         *dsk;
Partition    *prt;
FATFS        *fs;
*/

#undef printf
#define printf console_print

int console_print(const char *fmt, ...)
{
	static char str[256];

    va_list ap;
    int ret;
	char *pnt = str;
	
    va_start(ap, fmt);
	if(screen) {
	    ret = _vprintf(_string_write_char, (void **)&pnt, fmt, ap);
	    _string_write_char(0, (void **)&pnt);
	    screen->output(str);
	} else {
		ret = _vprintf(_diag_write_char, (void **)&pnt, fmt, ap);
	}
    va_end(ap);
    return (ret);
}

/*
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
    if(fs->check_fs(prt)) {
        printf("Did not find FAT file system.\n");
        delete fs;
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
	if(!blk->init())
		blk->set_state(e_device_ready);

    return init_fat();
}

int save_flash(char *filename)
{
    FATFIL *file = new FATFIL(fs);
    int res = file->open(filename, 0, FA_READ | FA_WRITE | FA_CREATE_ALWAYS );
    printf("File %s open result = %d.\n", filename, res);
    if(res != FR_OK) {
        return 0;
    }
    UINT bytes_written = 0;
	BYTE *buffer = new BYTE[33 * 512];
	int addr = 0;
	for(int i=0;i<128;i++) {
		flash.read(addr, 33*512, buffer);
	    res = file->write(buffer, 33*512, &bytes_written);
	    printf("Bytes written: %d (0x%6x)\n", bytes_written, addr);
	    addr += (33*512);
	}    
    file->close();
    delete file;
    delete buffer;
}
*/

bool need_update(int id, char *version, char *descr)
{
	t_flash_address image_address;
	flash->get_image_addresses(id, &image_address);
	int address = image_address.start;

    char current[16];
    flash->read_linear_addr(address + 4, 12, current);
    current[12] = 0;
    if((current[0] > ' ') && (current[0] <= 'z')) {
        printf("Version of \033\027%s\033\037 found:\n\033\027%s\033\037. New version = \033\027%s\033\037\n\n", descr, current, version);
    } else {
    	printf("Unknown version of \033\027%s\033\037\n", descr);
    	return true;
    }
    return (strcmp(version, current)!=0);
}

static int last_sector = -1;	

void flash_buffer(int id, void *buffer, int length, char *version, char *descr)
{
	t_flash_address image_address;
	flash->get_image_addresses(id, &image_address);
	int address = image_address.start;
    int page_size = flash->get_page_size();
    int page = address / page_size;

    printf("            \n");
    printf("Flashing  \033\027%s\033\037,\n  version \033\027%s\033\037..\n", descr, version);
    
    BYTE *bin = new BYTE[length+16];
    DWORD *pul;
    pul = (DWORD *)bin;
    *(pul++) = (DWORD)length;
    memset(pul, 0, 12);
    strcpy((char*)pul, version);
    memcpy(bin+16, buffer, length);                

	bool do_erase = flash->need_erase();
	int sector;
    char *p = (char *)bin;
    length+=16;
    while(length > 0) {
		if (do_erase) {
			sector = flash->page_to_sector(page);
			if (sector != last_sector) {
				last_sector = sector;
				printf("Erase %d   \r", sector);
				if(!flash->erase_sector(sector)) {
			        user_interface->popup("Programming failed...", BUTTON_CANCEL);
			        return;
				}
			}
		}
        printf("Page %d  \r", page);
        flash->write_page(page, p);
        page ++;
        p += page_size;
        length -= page_size;
    }    
}

int main()
{
	char time_buffer[32];
	printf("*** Ultimate Updater ***\n\n");

    c64 = new C64;
	c64->reset();
    c64->freeze();

    screen = new Screen(c64->get_screen(), c64->get_color_map(), 40, 25);
    screen->move_cursor(0,0);
    screen->output("\033\021   **** 1541 Ultimate II Updater ****\n\033\037"); // \020 = alpha \021 = beta
    for(int i=0;i<40;i++)
        screen->output('\002');

    user_interface = new UserInterface;
    user_interface->init(c64, c64->get_keyboard());
    user_interface->set_screen(screen);

	flash = get_flash();

	printf("%s ", rtc.get_long_date(time_buffer, 32));
	printf("%s\n", rtc.get_time_string(time_buffer, 32));

	if(!flash) {
		user_interface->popup("Flash device not recognized.", BUTTON_CANCEL);
		c64->unfreeze(0, NULL);
		delete user_interface;
		delete screen;
	 	screen = NULL;
    	delete c64;
    	flash->reboot(0);
	    while(1)
	        ;
    }

    int _binary_ultimate_bin_length = (int)&_binary_ultimate_bin_end - (int)&_binary_ultimate_bin_start;
    int _binary_1st_boot_700_bin_length = (int)&_binary_1st_boot_700_bin_end - (int)&_binary_1st_boot_700_bin_start;
    int _binary_2nd_boot_bin_length = (int)&_binary_2nd_boot_bin_end - (int)&_binary_2nd_boot_bin_start;

	bool do_update1 = false;
	bool do_update2 = false;

    do_update1 = need_update(FLASH_ID_BOOTFPGA, FPGA_VERSION, "FPGA") ||
                 need_update(FLASH_ID_BOOTAPP, BOOT_VERSION, "Secondary bootloader");
    do_update2 = need_update(FLASH_ID_APPL, APPL_VERSION, "Ultimate application");

	if(do_update1 || do_update2) {
        if(user_interface->popup("Update required. Continue?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
			flash->protect_disable();
			last_sector = -1;
			if(do_update1) {
			    flash_buffer(FLASH_ID_BOOTFPGA, &_binary_1st_boot_700_bin_start, _binary_1st_boot_700_bin_length, FPGA_VERSION, "FPGA");
			    flash_buffer(FLASH_ID_BOOTAPP, &_binary_2nd_boot_bin_start, _binary_2nd_boot_bin_length, BOOT_VERSION, "Secondary bootloader");
			}
			if(do_update2) {
		    	flash_buffer(FLASH_ID_APPL, &_binary_ultimate_bin_start, _binary_ultimate_bin_length, APPL_VERSION, "Ultimate application");
		    }
			printf("            \nDone!\n");
        }
    } else {
    	printf("No update required.\n");
	}

	printf("\nConfiguring Flash write protection..\n");
	flash->protect_configure();
	flash->protect_enable();
	
//	printf("Erasing configuration sector.\n");
//	flash.erase_sector(0x20FFFF);

	printf("\nPlease remove the SD card, and switch\noff your machine...\n");
    printf("\nTo avoid loading this updater again,\ndelete it from your media.\n");

    while(1)
        ;

/*
	wait_ms(5000);
	delete screen;
 	screen = NULL;
    delete c64;
*/
}
