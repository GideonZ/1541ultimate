#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "versions.h"

extern "C" {
	#include "itu.h"
    #include "small_printf.h"
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
#include "userinterface.h"

extern BYTE _binary_ultimate_bin_start;
extern BYTE _binary_ultimate_bin_end;
extern BYTE _binary_1st_boot_700_bin_start;
extern BYTE _binary_1st_boot_700_bin_end;
extern BYTE _binary_2nd_boot_bin_start;
extern BYTE _binary_2nd_boot_bin_end;

extern BYTE _binary_1541_ii_bin_start;
extern BYTE _binary_1541_bin_start;
extern BYTE _binary_1541c_bin_start;
extern BYTE _binary_ar5ntsc_bin_start;
extern BYTE _binary_ar5pal_bin_start;
extern BYTE _binary_ar6pal_bin_start;
extern BYTE _binary_epyx_bin_start;
extern BYTE _binary_final3_bin_start;
extern BYTE _binary_rr38ntsc_bin_start;
extern BYTE _binary_rr38pal_bin_start;
extern BYTE _binary_sounds_bin_start;
extern BYTE _binary_ss5ntsc_bin_start;
extern BYTE _binary_ss5pal_bin_start;
extern BYTE _binary_tar_ntsc_bin_start;
extern BYTE _binary_tar_pal_bin_start;

extern BYTE _binary_1541_ii_bin_end;
extern BYTE _binary_1541_bin_end;
extern BYTE _binary_1541c_bin_end;
extern BYTE _binary_ar5ntsc_bin_end;
extern BYTE _binary_ar5pal_bin_end;
extern BYTE _binary_ar6pal_bin_end;
extern BYTE _binary_epyx_bin_end;
extern BYTE _binary_final3_bin_end;
extern BYTE _binary_rr38ntsc_bin_end;
extern BYTE _binary_rr38pal_bin_end;
extern BYTE _binary_sounds_bin_end;
extern BYTE _binary_ss5ntsc_bin_end;
extern BYTE _binary_ss5pal_bin_end;
extern BYTE _binary_tar_ntsc_bin_end;
extern BYTE _binary_tar_pal_bin_end;

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

void flash_buffer(int id, void *buffer, void *buf_end, char *version, char *descr)
{
    int length = (int)buf_end - (int)buffer;
	t_flash_address image_address;
	flash->get_image_addresses(id, &image_address);
	int address = image_address.start;
    int page_size = flash->get_page_size();
    int page = address / page_size;
    char *p;
    
    printf("            \n");
    if(image_address.has_header) {
        printf("Flashing  \033\027%s\033\037,\n  version \033\027%s\033\037..\n", descr, version);
        BYTE *bin = new BYTE[length+16];
        DWORD *pul;
        pul = (DWORD *)bin;
        *(pul++) = (DWORD)length;
        memset(pul, 0, 12);
        strcpy((char*)pul, version);
        memcpy(bin+16, buffer, length);
        length+=16;
        p = (char *)bin;
    }
    else {
        printf("Flashing  \033\027%s\033\037..\n", descr);
        p = (char *)buffer;
    }    
    
	bool do_erase = flash->need_erase();
	int sector;
    while(length > 0) {
		if (do_erase) {
			sector = flash->page_to_sector(page);
			if (sector != last_sector) {
				last_sector = sector;
//				printf("Erase %d   \r", sector);
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

void copy_rom(BYTE *roms, int id, int *min, int *max, BYTE *source, BYTE *source_end)
{
	t_flash_address image_address;
	flash->get_image_addresses(id, &image_address);
    if(image_address.start < *min)
        *min = image_address.start;
    if((image_address.start + image_address.max_length) > *max)
        *max = image_address.start + image_address.max_length;
    int length = (int)source_end - (int)source;
    memcpy(roms+image_address.start, source, length);
}

bool program_flash(bool do_update1, bool do_update2, bool do_roms)
{
	BYTE *roms;
	
	flash->protect_disable();
	last_sector = -1;
	if(do_update1) {
	    flash_buffer(FLASH_ID_BOOTFPGA, &_binary_1st_boot_700_bin_start, &_binary_1st_boot_700_bin_end, FPGA_VERSION, "FPGA");
	    flash_buffer(FLASH_ID_BOOTAPP, &_binary_2nd_boot_bin_start, &_binary_2nd_boot_bin_end, BOOT_VERSION, "Secondary bootloader");
	}
    if(do_roms) {
        if(flash->get_page_size() == 528) { // atmel 45.. we should program the roms in one block!
            roms = (BYTE *)malloc(0x210000);
            memset(roms, 0xFF, 0x210000);
            int min = 0x210000;
            int max = 0x000000;
            copy_rom(roms, FLASH_ID_AR5PAL,    &min, &max, &_binary_ar5pal_bin_start,   &_binary_ar5pal_bin_end   );
            copy_rom(roms, FLASH_ID_AR6PAL,    &min, &max, &_binary_ar6pal_bin_start,   &_binary_ar6pal_bin_end   );
            copy_rom(roms, FLASH_ID_FINAL3,    &min, &max, &_binary_final3_bin_start,   &_binary_final3_bin_end   );
            copy_rom(roms, FLASH_ID_SOUNDS,    &min, &max, &_binary_sounds_bin_start,   &_binary_sounds_bin_end   );
            copy_rom(roms, FLASH_ID_EPYX,      &min, &max, &_binary_epyx_bin_start,     &_binary_epyx_bin_end     );
            copy_rom(roms, FLASH_ID_ROM1541,   &min, &max, &_binary_1541_bin_start,     &_binary_1541_bin_end     );
            copy_rom(roms, FLASH_ID_RR38PAL,   &min, &max, &_binary_rr38pal_bin_start,  &_binary_rr38pal_bin_end  );
            copy_rom(roms, FLASH_ID_SS5PAL,    &min, &max, &_binary_ss5pal_bin_start,   &_binary_ss5pal_bin_end   );
            copy_rom(roms, FLASH_ID_AR5NTSC,   &min, &max, &_binary_ar5ntsc_bin_start,  &_binary_ar5ntsc_bin_end  );
            copy_rom(roms, FLASH_ID_ROM1541C,  &min, &max, &_binary_1541c_bin_start,    &_binary_1541c_bin_end    );
            copy_rom(roms, FLASH_ID_ROM1541II, &min, &max, &_binary_1541_ii_bin_start,  &_binary_1541_ii_bin_end  );
            copy_rom(roms, FLASH_ID_RR38NTSC,  &min, &max, &_binary_rr38ntsc_bin_start, &_binary_rr38ntsc_bin_end );
            copy_rom(roms, FLASH_ID_SS5NTSC,   &min, &max, &_binary_ss5ntsc_bin_start,  &_binary_ss5ntsc_bin_end  );
            copy_rom(roms, FLASH_ID_TAR_PAL,   &min, &max, &_binary_tar_pal_bin_start,  &_binary_tar_pal_bin_end  );
            copy_rom(roms, FLASH_ID_TAR_NTSC,  &min, &max, &_binary_tar_ntsc_bin_start, &_binary_tar_ntsc_bin_end );
            // printf("All roms located from %p to %p.\n", min, max);
            flash_buffer(FLASH_ID_ALL_ROMS, roms + min, roms + max, "", "all roms");
            free(roms);
        } else {
    	    flash_buffer(FLASH_ID_AR5PAL,    &_binary_ar5pal_bin_start,   &_binary_ar5pal_bin_end,   "", "ar5pal");
    	    flash_buffer(FLASH_ID_AR6PAL,    &_binary_ar6pal_bin_start,   &_binary_ar6pal_bin_end,   "", "ar6pal");
    	    flash_buffer(FLASH_ID_FINAL3,    &_binary_final3_bin_start,   &_binary_final3_bin_end,   "", "final3");
    	    flash_buffer(FLASH_ID_SOUNDS,    &_binary_sounds_bin_start,   &_binary_sounds_bin_end,   "", "sounds");
    	    flash_buffer(FLASH_ID_EPYX,      &_binary_epyx_bin_start,     &_binary_epyx_bin_end,     "", "epyx");
    	    flash_buffer(FLASH_ID_ROM1541,   &_binary_1541_bin_start,     &_binary_1541_bin_end,     "", "1541");
    	    flash_buffer(FLASH_ID_RR38PAL,   &_binary_rr38pal_bin_start,  &_binary_rr38pal_bin_end,  "", "rr38pal");
    	    flash_buffer(FLASH_ID_SS5PAL,    &_binary_ss5pal_bin_start,   &_binary_ss5pal_bin_end,   "", "ss5pal");
    	    flash_buffer(FLASH_ID_AR5NTSC,   &_binary_ar5ntsc_bin_start,  &_binary_ar5ntsc_bin_end,  "", "ar5ntsc");
    	    flash_buffer(FLASH_ID_ROM1541C,  &_binary_1541c_bin_start,    &_binary_1541c_bin_end,    "", "1541c");
    	    flash_buffer(FLASH_ID_ROM1541II, &_binary_1541_ii_bin_start,  &_binary_1541_ii_bin_end,  "", "1541-ii");
    	    flash_buffer(FLASH_ID_RR38NTSC,  &_binary_rr38ntsc_bin_start, &_binary_rr38ntsc_bin_end, "", "rr38ntsc");
    	    flash_buffer(FLASH_ID_SS5NTSC,   &_binary_ss5ntsc_bin_start,  &_binary_ss5ntsc_bin_end,  "", "ss5ntsc");
    	    flash_buffer(FLASH_ID_TAR_PAL,   &_binary_tar_pal_bin_start,  &_binary_tar_pal_bin_end,  "", "tar_pal");
    	    flash_buffer(FLASH_ID_TAR_NTSC,  &_binary_tar_ntsc_bin_start, &_binary_tar_ntsc_bin_end, "", "tar_ntsc");
        }
    }
	if(do_update2) {
    	flash_buffer(FLASH_ID_APPL, &_binary_ultimate_bin_start, &_binary_ultimate_bin_end, APPL_VERSION, "Ultimate application");
    }
	printf("            \nDone!\n");
	return true;
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
	    while(1)
	        ;
    }

	bool do_update1 = false;
	bool do_update2 = false;
    bool virgin = false;

    // virginity check
	t_flash_address image_address;
	flash->get_image_addresses(FLASH_ID_AR5PAL, &image_address);
    flash->read_linear_addr(image_address.start, 2, time_buffer);
    if(time_buffer[0] == 0xFF)
        virgin = true;
        
    do_update1 = need_update(FLASH_ID_BOOTFPGA, FPGA_VERSION, "FPGA") ||
                 need_update(FLASH_ID_BOOTAPP, BOOT_VERSION, "Secondary bootloader");
    do_update2 = need_update(FLASH_ID_APPL, APPL_VERSION, "Ultimate application");

    if(virgin) {
        program_flash(do_update1, do_update2, true);
    } else if(do_update1 || do_update2) {
        if(user_interface->popup("Update required. Continue?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
            program_flash(do_update1, do_update2, false);
        }
    } else {
        if(user_interface->popup("Update NOT required. Force?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
            program_flash(true, true, true);
        }
	}

    if(!virgin) {
        if (user_interface->popup("Reset configuration?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
            int num = flash->get_number_of_config_pages();
            for(int i=0;i<num;i++) {
                flash->clear_config_page(i);
            }
        }
    }

	printf("\nConfiguring Flash write protection..\n");
	flash->protect_configure();
	flash->protect_enable();
    printf("\nTo avoid loading this updater again,\ndelete it from your media.\n");
	
    wait_ms(2000);
    
    user_interface->popup("Remove SDCard. Reboot?", BUTTON_OK);
    flash->reboot(0);
    
//	printf("\nPlease remove the SD card, and switch\noff your machine...\n");

    while(1)
        ;
}

