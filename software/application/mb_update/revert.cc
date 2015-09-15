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

extern uint8_t _ultimate_bin_start;
extern uint8_t _ultimate_bin_end;
extern uint8_t _1st_boot_700_bin_start;
extern uint8_t _1st_boot_700_bin_end;
extern uint8_t _2nd_boot_bin_start;
extern uint8_t _2nd_boot_bin_end;

extern uint8_t _1541_ii_bin_start;
extern uint8_t _1541_bin_start;
extern uint8_t _1541c_bin_start;
extern uint8_t _ar5ntsc_bin_start;
extern uint8_t _ar5pal_bin_start;
extern uint8_t _ar6pal_bin_start;
extern uint8_t _epyx_bin_start;
extern uint8_t _final3_bin_start;
extern uint8_t _rr38ntsc_bin_start;
extern uint8_t _rr38pal_bin_start;
extern uint8_t _sounds_bin_start;
extern uint8_t _ss5ntsc_bin_start;
extern uint8_t _ss5pal_bin_start;
extern uint8_t _tar_ntsc_bin_start;
extern uint8_t _tar_pal_bin_start;

extern uint8_t _1541_ii_bin_end;
extern uint8_t _1541_bin_end;
extern uint8_t _1541c_bin_end;
extern uint8_t _ar5ntsc_bin_end;
extern uint8_t _ar5pal_bin_end;
extern uint8_t _ar6pal_bin_end;
extern uint8_t _epyx_bin_end;
extern uint8_t _final3_bin_end;
extern uint8_t _rr38ntsc_bin_end;
extern uint8_t _rr38pal_bin_end;
extern uint8_t _sounds_bin_end;
extern uint8_t _ss5ntsc_bin_end;
extern uint8_t _ss5pal_bin_end;
extern uint8_t _tar_ntsc_bin_end;
extern uint8_t _tar_pal_bin_end;

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


bool need_update(int id, char *version, char *descr)
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

static int last_sector = -1;	

bool flash_buffer(int id, void *buffer, void *buf_end, char *version, char *descr)
{
    int length = (int)buf_end - (int)buffer;
	t_flash_address image_address;
	flash->get_image_addresses(id, &image_address);
	int address = image_address.start;
    int page_size = flash->get_page_size();
    int page = address / page_size;
    char *p;
    char *verify_buffer = new char[page_size]; // is never freed, but the hell we care!

    //console_print(screen, "            \n");
    if(image_address.has_header) {
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

void copy_rom(uint8_t *roms, int id, int *min, int *max, uint8_t *source, uint8_t *source_end)
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
	uint8_t *roms;
	
	flash->protect_disable();
	last_sector = -1;
    int fpga_type = (getFpgaCapabilities() & CAPAB_FPGA_TYPE) >> FPGA_TYPE_SHIFT;
	if(do_update1) {
        bool ok;
        
        do {
            switch(fpga_type) {
                case 0:
        	        ok = flash_buffer(FLASH_ID_BOOTFPGA, &_1st_boot_700_bin_start, &_1st_boot_700_bin_end, FPGA_VERSION, "FPGA");
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
            ok = flash_buffer(FLASH_ID_BOOTAPP, &_2nd_boot_bin_start, &_2nd_boot_bin_end, BOOT_VERSION, "Secondary bootloader");
            if(!ok)
                user_interface->popup("Critical update failed. Retry?", BUTTON_OK);
        } while(!ok);
	}
    if(do_roms) {
        if(flash->get_page_size() == 528) { // atmel 45.. we should program the roms in one block!
            roms = (uint8_t *)malloc(0x210000);
            memset(roms, 0xFF, 0x210000);
            int min = 0x210000;
            int max = 0x000000;
            copy_rom(roms, FLASH_ID_AR5PAL,    &min, &max, &_ar5pal_bin_start,   &_ar5pal_bin_end   );
            copy_rom(roms, FLASH_ID_AR6PAL,    &min, &max, &_ar6pal_bin_start,   &_ar6pal_bin_end   );
            copy_rom(roms, FLASH_ID_FINAL3,    &min, &max, &_final3_bin_start,   &_final3_bin_end   );
            copy_rom(roms, FLASH_ID_SOUNDS,    &min, &max, &_sounds_bin_start,   &_sounds_bin_end   );
            copy_rom(roms, FLASH_ID_EPYX,      &min, &max, &_epyx_bin_start,     &_epyx_bin_end     );
            copy_rom(roms, FLASH_ID_ROM1541,   &min, &max, &_1541_bin_start,     &_1541_bin_end     );
            copy_rom(roms, FLASH_ID_RR38PAL,   &min, &max, &_rr38pal_bin_start,  &_rr38pal_bin_end  );
            copy_rom(roms, FLASH_ID_SS5PAL,    &min, &max, &_ss5pal_bin_start,   &_ss5pal_bin_end   );
            copy_rom(roms, FLASH_ID_AR5NTSC,   &min, &max, &_ar5ntsc_bin_start,  &_ar5ntsc_bin_end  );
            copy_rom(roms, FLASH_ID_ROM1541C,  &min, &max, &_1541c_bin_start,    &_1541c_bin_end    );
            copy_rom(roms, FLASH_ID_ROM1541II, &min, &max, &_1541_ii_bin_start,  &_1541_ii_bin_end  );
            copy_rom(roms, FLASH_ID_RR38NTSC,  &min, &max, &_rr38ntsc_bin_start, &_rr38ntsc_bin_end );
            copy_rom(roms, FLASH_ID_SS5NTSC,   &min, &max, &_ss5ntsc_bin_start,  &_ss5ntsc_bin_end  );
            copy_rom(roms, FLASH_ID_TAR_PAL,   &min, &max, &_tar_pal_bin_start,  &_tar_pal_bin_end  );
            copy_rom(roms, FLASH_ID_TAR_NTSC,  &min, &max, &_tar_ntsc_bin_start, &_tar_ntsc_bin_end );
            // console_print(screen, "All roms located from %p to %p.\n", min, max);
            flash_buffer(FLASH_ID_ALL_ROMS, roms + min, roms + max, "", "all roms");
            free(roms);
        } else {
    	    flash_buffer(FLASH_ID_AR5PAL,    &_ar5pal_bin_start,   &_ar5pal_bin_end,   "", "ar5pal");
    	    flash_buffer(FLASH_ID_AR6PAL,    &_ar6pal_bin_start,   &_ar6pal_bin_end,   "", "ar6pal");
    	    flash_buffer(FLASH_ID_FINAL3,    &_final3_bin_start,   &_final3_bin_end,   "", "final3");
    	    flash_buffer(FLASH_ID_SOUNDS,    &_sounds_bin_start,   &_sounds_bin_end,   "", "sounds");
    	    flash_buffer(FLASH_ID_EPYX,      &_epyx_bin_start,     &_epyx_bin_end,     "", "epyx");
    	    flash_buffer(FLASH_ID_ROM1541,   &_1541_bin_start,     &_1541_bin_end,     "", "1541");
    	    flash_buffer(FLASH_ID_RR38PAL,   &_rr38pal_bin_start,  &_rr38pal_bin_end,  "", "rr38pal");
    	    flash_buffer(FLASH_ID_SS5PAL,    &_ss5pal_bin_start,   &_ss5pal_bin_end,   "", "ss5pal");
    	    flash_buffer(FLASH_ID_AR5NTSC,   &_ar5ntsc_bin_start,  &_ar5ntsc_bin_end,  "", "ar5ntsc");
    	    flash_buffer(FLASH_ID_ROM1541C,  &_1541c_bin_start,    &_1541c_bin_end,    "", "1541c");
    	    flash_buffer(FLASH_ID_ROM1541II, &_1541_ii_bin_start,  &_1541_ii_bin_end,  "", "1541-ii");
    	    flash_buffer(FLASH_ID_RR38NTSC,  &_rr38ntsc_bin_start, &_rr38ntsc_bin_end, "", "rr38ntsc");
    	    flash_buffer(FLASH_ID_SS5NTSC,   &_ss5ntsc_bin_start,  &_ss5ntsc_bin_end,  "", "ss5ntsc");
    	    flash_buffer(FLASH_ID_TAR_PAL,   &_tar_pal_bin_start,  &_tar_pal_bin_end,  "", "tar_pal");
    	    flash_buffer(FLASH_ID_TAR_NTSC,  &_tar_ntsc_bin_start, &_tar_ntsc_bin_end, "", "tar_ntsc");
        }
    }
	if(do_update2) {
    	flash_buffer(FLASH_ID_APPL, &_ultimate_bin_start, &_ultimate_bin_end, APPL_VERSION, "Ultimate application");
    }
	console_print(screen, "                       \nDone!\n");
	return true;
}
    
int main()
{
	char time_buffer[32];
	uint8_t byte_buffer[32];

	printf("*** Ultimate Reverter ***\n\n");
	flash = get_flash();
    printf("Flash = %p. Capabilities = %8x\n", flash, getFpgaCapabilities());

    GenericHost *host = 0;
    Stream *stream = new Stream_UART;
    C64 *c64 = new C64;
    c64->reset();

    if (c64->exists()) {
    	host = c64;
    } else {
    	host = new HostStream(stream);
    }

    printf("host = %p\n", host);
	host->take_ownership(NULL);

    screen = host->getScreen();
    screen->clear();
    screen->output("\033\021   **** 1541 Ultimate II Reverter ****\n\033\037"); // \020 = alpha \021 = beta
    screen->repeat('\002', 40);

    user_interface = new UserInterface;
    user_interface->init(host);
    user_interface->set_screen(screen);

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
    if(calc_checksum(&_1st_boot_700_bin_start, &_1st_boot_700_bin_end) == CHK_1st_boot_700_bin)
        console_print(screen, "Checksum of FPGA image:   OK..\n");
    else {
        console_print(screen, check_error);
        while(1);
    }
    if(calc_checksum(&_2nd_boot_bin_start, &_2nd_boot_bin_end) == CHK_2nd_boot_bin)
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
            
    if(user_interface->popup("Reverting to 2.6k. Continue?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
        program_flash(true, true, true);
    }

	console_print(screen, "\nConfiguring Flash write protection..\n");
	flash->protect_configure();
	flash->protect_enable();

	if (user_interface->popup("Reset configuration?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
		int num = flash->get_number_of_config_pages();
		for(int i=0;i<num;i++) {
			flash->clear_config_page(i);
		}
	}

//    console_print(screen, "\nTo avoid loading this updater again,\ndelete it from your media.\n");
	
//    wait_ms(2000);
    
//    user_interface->popup("Remove SDCard. Reboot?", BUTTON_OK);
    flash->reboot(0);
    
    while(1)
        ;
}

