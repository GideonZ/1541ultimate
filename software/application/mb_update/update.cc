#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "versions.h"

extern "C" {
	#include "itu.h"
    #include "small_printf.h"
    #include "dump_hex.h"
}

#include "host.h"
#include "c64.h"
#include "c64_crt.h"
#include "overlay.h"
#include "flash.h"
#include "screen.h"
#include "keyboard.h"
#include "config.h"
#include "rtc.h"
#include "userinterface.h"
#include "checksums.h"
#include "stream.h"
#include "stream_uart.h"
#include "host_stream.h"

#include "filemanager.h"
#include "init_function.h"

extern uint8_t _ultimate_bin_start;
extern uint8_t _ultimate_bin_end;
extern uint8_t _mb_boot_700_b_start;
extern uint8_t _mb_boot_700_b_end;
extern uint8_t _mb_boot2_bin_start;
extern uint8_t _mb_boot2_bin_end;

extern uint8_t _1541_bin_start;
extern uint8_t _snds1541_bin_start;

Screen *screen;
UserInterface *user_interface;
Flash *flash;

#undef printf
//#define printf(x) console_print(screen, x)

int calc_checksum(uint8_t *buffer, uint8_t *buffer_end)
{
    int check = 0;
    int b;
    while(buffer != buffer_end) {
        b = (int)*buffer;
        if(check < 0)
            check = check ^ 0x801618D5; // makes it positive! ;)
        check <<= 1;
        check += b;
        buffer++;
    }
    return check;
}


bool need_update(int id, const char *version, const char *descr)
{
	t_flash_address image_address;
	flash->get_image_addresses(id, &image_address);
	int address = image_address.start;

    char current[16];
    flash->read_linear_addr(address + 4, 12, current);
    current[12] = 0;
    if((current[0] > ' ') && (current[0] <= 'z')) {
        console_print(screen, "Version of \033\027%s\033\037 found:\n\033\027%s\033\037. New version: \033\027%s\033\037\n", descr, current, version);
    } else {
    	console_print(screen, "Unknown version of \033\027%s\033\037\n", descr);
    	return true;
    }
    return (strcmp(version, current)!=0);
}

bool my_memcmp(void *a, void *b, int len)
{
    uint32_t *pula = (uint32_t *)a;
    uint32_t *pulb = (uint32_t *)b;
    len >>= 2;
    while(len--) {
        if(*pula != *pulb) {
            console_print(screen, "ERR: %p: %8x, %p %8x.\n", pula, *pula, pulb, *pulb);
            return false;
        }
        pula++;
        pulb++;
    }
    return true;
}

static void create_dir(const char *name)
{
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->create_dir(name);
    console_print(screen, "Creating dir '%s': %s\n", name, FileSystem :: get_error_string(fres));
}

static void write_flash_file(const char *path, const char *name, uint8_t *data, int length)
{
    File *f;
    uint32_t dummy;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(path, name, FA_CREATE_ALWAYS | FA_WRITE, &f);
    if (fres == FR_OK) {
        fres = f->write(data, length, &dummy);
        console_print(screen, "Writing %s: %s\n", name, FileSystem :: get_error_string(fres));
        fm->fclose(f);
    }
}

static int last_sector = -1;	

bool flash_buffer(int id, void *buffer, void *buf_end, const char *version, const char *descr)
{
    int length = (int)buf_end - (int)buffer;
	t_flash_address image_address;
	flash->get_image_addresses(id, &image_address);
	if (image_address.id == FLASH_ID_LIST_END) {
		console_print(screen, "ROM image obsolete: No target: %s\n", descr);
		return true;
	}

	if (image_address.max_length < length) {
	    console_print(screen, "\033\032** PANIC: %s too large to fit in flash. Aborted.\n", descr);
	    console_print(screen, "\033\022-> Exceeded by %d bytes.\n", length - image_address.max_length);
	    while (1);
	}

	int address = image_address.start;
    int page_size = flash->get_page_size();
    int page = address / page_size;
    char *p;
    char *verify_buffer = new char[page_size]; // is never freed, but the hell we care!

    //console_print(screen, "            \n");
    if(image_address.has_header) {
        // dump_hex(buffer, 256);
        console_print(screen, "Flashing  \033\027%s\033\037,\n  version \033\027%s\033\037..\n", descr, version);
        uint8_t *bin = new uint8_t[length+16];
        uint32_t *pul;
        pul = (uint32_t *)bin;
        *(pul++) = (uint32_t)length;
        memset(pul, 0, 12);
        strcpy((char*)pul, version);
        memcpy(bin+16, buffer, length);
        length+=16;
        p = (char *)bin;
        // dump_hex(p, 256);
    }
    else {
        console_print(screen, "Flashing  \033\027%s\033\037..\n", descr);
        p = (char *)buffer;
    }    
    
	bool do_erase = flash->need_erase();
	int sector;
    while(length > 0) {
		if (do_erase) {
			sector = flash->page_to_sector(page);
			if (sector != last_sector) {
				last_sector = sector;
				// console_print(screen, "Erasing     %d   \r", sector);
				if(!flash->erase_sector(sector)) {
			        // user_interface->popup("Erasing failed...", BUTTON_CANCEL);
			        return false;
				}
			}
		}
        console_print(screen, "Programming %d  \r", page);
        int retry = 3;
        while(retry > 0) {
            retry --;
            if(!flash->write_page(page, p)) {
                console_print(screen, "Programming error on page %d.\n", page);
                continue;    
            }
            flash->read_page(page, verify_buffer);
            if(!my_memcmp(verify_buffer, p, page_size)) {
                console_print(screen, "Verify failed on page %d. Retry %d.\n", page, retry);
                console_print(screen, "%p %p %d\n", verify_buffer, p, page_size);
                dump_hex(verify_buffer, 32);
                dump_hex(p, 32);
                continue;
            }
            retry = -2;
        }
        if(retry != -2) {                
            //user_interface->popup("Programming failed...", BUTTON_CANCEL);
            return false;
        }
        page ++;
        p += page_size;
        length -= page_size;
    }

    return true;    
}

bool program_flash(bool do_update1, bool do_update2)
{
    create_dir(ROMS_DIRECTORY);
    create_dir(CARTS_DIRECTORY);
    write_flash_file(ROMS_DIRECTORY, "1541.rom", &_1541_bin_start, 0x4000);
    write_flash_file(ROMS_DIRECTORY, "snds1541.bin", &_snds1541_bin_start, 0xC000);

	flash->protect_disable();
	last_sector = -1;
    int fpga_type = (getFpgaCapabilities() & CAPAB_FPGA_TYPE) >> FPGA_TYPE_SHIFT;
	if(do_update1) {
        bool ok;
        
        do {
            switch(fpga_type) {
                case 0:
        	        ok = flash_buffer(FLASH_ID_BOOTFPGA, &_mb_boot_700_b_start, &_mb_boot_700_b_end, FPGA_VERSION, "FPGA");
        	        break;
#if _FPGA400 == 1
        	    case 1:
        	        ok = flash_buffer(FLASH_ID_BOOTFPGA, &_1st_boot_400_bin_start, &_1st_boot_400_bin_end, FPGA_VERSION, "FPGA");
        	        break;
#endif
                default:
                    console_print(screen, "ERROR: Unknown FPGA type detected.\n");
                    while(1);
            } 
            if(!ok)
                user_interface->popup("Critical update failed. Retry?", BUTTON_OK);
        } while(!ok);
        do {
            ok = flash_buffer(FLASH_ID_BOOTAPP, &_mb_boot2_bin_start, &_mb_boot2_bin_end, BOOT_VERSION, "Secondary bootloader");
            if(!ok)
                user_interface->popup("Critical update failed. Retry?", BUTTON_OK);
        } while(!ok);
	}
	if(do_update2) {
    	flash_buffer(FLASH_ID_APPL, &_ultimate_bin_start, &_ultimate_bin_end, APPL_VERSION, "Ultimate application");
    }
	console_print(screen, "                       \nDone!\n");
	return true;
}

extern "C" void ultimate_main(void *)
{
	char time_buffer[32];
	uint8_t byte_buffer[32];

	printf("*** Ultimate Updater ***\n\n");
	flash = get_flash();
    printf("Flash = %p. Capabilities = %8x\n", flash, getFpgaCapabilities());

    GenericHost *host = 0;
    Stream *stream = new Stream_UART;
    InitFunction :: executeAll();

    C64 *c64 = C64 :: getMachine();
    c64->reset();

    if (c64->exists()) {
    	host = c64;
    } else {
    	host = new HostStream(stream);
    }
    
    printf("host = %p\n", host);

    UserInterface *user_interface = new UserInterface("\033\021** 1541 Ultimate II Updater **\n\033\037");
    user_interface->init(host);
    host->take_ownership(user_interface);
    user_interface->appear();

    screen = host->getScreen();
    screen->move_cursor(0, 2);
    console_print(screen, "%s ", rtc.get_long_date(time_buffer, 32));
	console_print(screen, "%s\n", rtc.get_time_string(time_buffer, 32));

	if(!flash) {
		user_interface->popup("Flash device not recognized.", BUTTON_CANCEL);
		host->release_ownership();
		delete user_interface;
	 	screen = NULL;
    	delete host;
	    while(1)
	        ;
    }

    /* Extra check on the loaded images */
    const char *check_error = "\033\032\nChecksum error... Not flashing.\n";
    console_print(screen, "\033\027Checking checksums loaded images..\033\037\n");
    if(calc_checksum(&_mb_boot_700_b_start, &_mb_boot_700_b_end) == CHK_mb_boot_700_b)
        console_print(screen, "Checksum of FPGA image:   OK..\n");
    else {
        console_print(screen, check_error);
        while(1);
    }
    if(calc_checksum(&_mb_boot2_bin_start, &_mb_boot2_bin_end) == CHK_mb_boot2_bin)
        console_print(screen, "Checksum of Bootloader:   OK..\n");
    else {
        console_print(screen, check_error);
        while(1);
    }
    if(calc_checksum(&_ultimate_bin_start, &_ultimate_bin_end) == CHK_ultimate_bin)
        console_print(screen, "Checksum of Application:  OK..\n\n");
    else {
        console_print(screen, check_error);
        while(1);
    }
    console_print(screen, "\n\n\n\n\n\n\n\n");
            
	bool do_update1 = false;
	bool do_update2 = false;
    bool virgin = false;

//	flash->protect_disable();
//    console_print(screen, "Flash protection disabled.\n");
//    while(1);
//

    do_update1 = need_update(FLASH_ID_BOOTFPGA, FPGA_VERSION, "FPGA") ||
                 need_update(FLASH_ID_BOOTAPP, BOOT_VERSION, "Secondary bootloader");
    do_update2 = need_update(FLASH_ID_APPL, APPL_VERSION, "Ultimate application");

    if(virgin) {
        program_flash(true, true);
    } else if(do_update1 || do_update2) {
        if(user_interface->popup("Update required. Continue?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
            program_flash(true, true);
        }
    } else {
        int response = user_interface->popup("Update NOT required. Force?", BUTTON_YES | BUTTON_NO);
        if(response == BUTTON_YES) {
            program_flash(true, true);
        }
	}

	console_print(screen, "\nConfiguring Flash write protection..\n");
	flash->protect_configure();
	flash->protect_enable();

    if(!virgin) {
        if (user_interface->popup("Reset configuration?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
            int num = flash->get_number_of_config_pages();
            for(int i=0;i<num;i++) {
                flash->clear_config_page(i);
            }
        }
    }

    flash->reboot(0);
    
    while(1)
        ;
}

