/*
 * prog_flash.cc
 *
 *  Created on: May 19, 2016
 *      Author: gideon
 */

#include "prog_flash.h"
#include <string.h>
#include <stdio.h>
#include "screen.h"
#include "dump_hex.h"

static bool my_memcmp(Screen *screen, void *a, void *b, int len)
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

bool flash_buffer(Flash *flash, Screen *screen, int id, void *buffer, void *buf_end, char *version, char *descr)
{
	t_flash_address image_address;
	flash->get_image_addresses(id, &image_address);
	int address = image_address.start;
	return flash_buffer_at(flash, screen, address, image_address.has_header, buffer, buf_end, version, descr);
}

bool flash_buffer_at(Flash *flash, Screen *screen, int address, bool header, void *buffer, void *buf_end, char *version, char *descr)
{
	static int last_sector = -1;
	int length = (int)buf_end - (int)buffer;
	if (length > 9000000) {
		return false;
	}
    int page_size = flash->get_page_size();
    int page = address / page_size;
    char *p;
    char *verify_buffer = new char[page_size]; // is never freed, but the hell we care!

    //console_print(screen, "            \n");
    if(header) {
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
				printf("Erasing     %d   \n", sector);
				if(!flash->erase_sector(sector)) {
			        // user_interface->popup("Erasing failed...", BUTTON_CANCEL);
					last_sector = -1;
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
            if(!my_memcmp(screen, verify_buffer, p, page_size)) {
                console_print(screen, "Verify failed on page %d.\n", page, retry);
                // console_print(screen, "%p %p %d\n", verify_buffer, p, page_size);
                dump_hex_verify(p, verify_buffer, page_size);
                continue;
            }
            retry = -2;
        }
        if(retry != -2) {
			//last_sector = -1;
            //user_interface->popup("Programming failed...", BUTTON_CANCEL);
            // return false;
        }
        page ++;
        p += page_size;
        length -= page_size;
    }
    return true;
}
