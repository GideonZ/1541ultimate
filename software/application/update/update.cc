#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "versions.h"

extern "C" {
	#include "itu.h"
}
#include "c64.h"
#include "spiflash.h"
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

void flash_buffer(int address, void *buffer, int length, char *version, char *descr)
{
    int page_size = flash.get_page_size();
    int page = address / page_size;

    printf("            \n");
    
    char current[16];
    flash.read(address + 4, 12, current);
    current[12] = 0;
    if((current[0] > ' ') && (current[0] <= 'z')) {
        printf("Version of \033\027%s\033\037 found:\n\033\027%s\033\037. New version = \033\027%s\033\037\n", descr, current, version);
    }
    if(strcmp(version, current)==0) {
        printf("%s already updated.\n", descr);
        return;
    }
    printf("Flashing \033\027%s\033\037, version \033\027%s\033\037..\n", descr, version);

    BYTE *bin = new BYTE[length+16];
    DWORD *pul;
    pul = (DWORD *)bin;
    *(pul++) = (DWORD)length;
    memset(pul, 0, 12);
    strcpy((char*)pul, version);
    memcpy(bin+16, buffer, length);                

    char *p = (char *)bin;
    length+=16;
    while(length > 0) {
        printf("Page %d\r", page);
        flash.write_page(page++, p);
        p += page_size;
        length -= page_size;
    }    
}

int main()
{
	char time_buffer[32];
	printf("*** Ultimate Updater ***\n\n");

    c64 = new C64;
    c64->freeze();

    screen = new Screen(c64->get_screen(), c64->get_color_map(), 40, 25);
    screen->move_cursor(0,0);
    screen->output("\033\021   **** 1541 Ultimate II Updater ****\n\033\037"); // \020 = alpha \021 = beta
    for(int i=0;i<40;i++)
        screen->output('\002');

    user_interface = new UserInterface;
    user_interface->init(c64, c64->get_keyboard());
    user_interface->set_screen(screen);

	printf("%s ", rtc.get_long_date(time_buffer, 32));
	printf("%s\n", rtc.get_time_string(time_buffer, 32));

    int _binary_ultimate_bin_length = (int)&_binary_ultimate_bin_end - (int)&_binary_ultimate_bin_start;
    int _binary_1st_boot_700_bin_length = (int)&_binary_1st_boot_700_bin_end - (int)&_binary_1st_boot_700_bin_start;
    int _binary_2nd_boot_bin_length = (int)&_binary_2nd_boot_bin_end - (int)&_binary_2nd_boot_bin_start;

/*
    static char popup_buf[64];
    sprintf(popup_buf, "Current FPGA rev $%b. Flash rev $94?", ITU_FPGA_VERSION);
    printf(popup_buf);
    
    if(ITU_FPGA_VERSION != 0x94) {
        if(user_interface->popup(popup_buf, BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
            printf("Flashing FPGA...\n");
            flash_buffer(0, &_binary_1st_boot_700_bin_start, _binary_1st_boot_700_bin_length, "FPGA U2 $94");
        }
    }
*/
    /* Don't ask, just do it! */
    flash_buffer(0, &_binary_1st_boot_700_bin_start, _binary_1st_boot_700_bin_length, "FPGA U2 $95", "FPGA");
    flash_buffer(FLASH_ADDR_BOOTAPP, &_binary_2nd_boot_bin_start, _binary_2nd_boot_bin_length, BOOT_VERSION, "Secondary bootloader");
    flash_buffer(FLASH_ADDR_APPL, &_binary_ultimate_bin_start, _binary_ultimate_bin_length, APPL_VERSION, "Ultimate application");
/*
	printf("Erasing configuration sector.\n");
	flash.erase_sector(0x20FFFF);
*/
	printf("            \nDone!\n");
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
