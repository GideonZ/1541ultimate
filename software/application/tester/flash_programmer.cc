/*
 * flash_programmer.cc
 *
 *  Created on: Nov 17, 2016
 *      Author: gideon
 */


#include <string.h>
#include <stdio.h>
#include "dump_hex.h"
#include "flash.h"
#include "u2p.h"
#include "system.h"
#include "altera_avalon_pio_regs.h"

static bool my_memcmp(void *a, void *b, int len)
{
    uint32_t *pula = (uint32_t *)a;
    uint32_t *pulb = (uint32_t *)b;
    len >>= 2;
    while(len--) {
        if(*pula != *pulb) {
            printf("ERR: %p: %8x, %p %8x.\n", pula, *pula, pulb, *pulb);
            return false;
        }
        pula++;
        pulb++;
    }
    return true;
}

extern "C" {
int doFlashProgram(uint32_t address, void *buffer, int length)
{
	static int last_sector = -1;

	if (address & 0x80000000) {
		REMOTE_FLASHSEL_1;
	} else {
		REMOTE_FLASHSEL_0;
	}
	REMOTE_FLASHSELCK_0;
	REMOTE_FLASHSELCK_1;

	address &= 0x7FFFFFFF;

	Flash *flash = get_flash();
	int page_size = flash->get_page_size();
    int page = address / page_size;
    char *p;
    char *verify_buffer = new char[page_size];

    flash->protect_disable();
    printf("Flashing...\n");

	p = (char *)buffer;

	bool do_erase = flash->need_erase();
	int retval = 0;
	int sector;
    while(length > 0) {
		if (do_erase) {
			sector = flash->page_to_sector(page);
			if (sector != last_sector) {
				last_sector = sector;
				printf("Sector %d   \r", sector);
				IOWR_ALTERA_AVALON_PIO_DATA(PIO_3_BASE, 1 << (sector & 3));
				if(!flash->erase_sector(sector)) {
			        // user_interface->popup("Erasing failed...", BUTTON_CANCEL);
					last_sector = -1;
					retval = -1;
					break;
				}
			}
		}
        // printf("Programming %d  \r", page);
        int retry = 3;
        while(retry > 0) {
            retry --;
            if(!flash->write_page(page, p)) {
                printf("Programming error on page %d.\n", page);
                continue;
            }
            flash->read_page(page, verify_buffer);
            if(!my_memcmp(verify_buffer, p, page_size)) {
                printf("Verify failed on page %d.\n", page, retry);
                // printf("%p %p %d\n", verify_buffer, p, page_size);
                dump_hex_verify(p, verify_buffer, page_size);
                continue;
            }
            retry = -2;
        }
        if(retry != -2) {
			last_sector = -1;
			retval = -2;
			break;
        }
        page ++;
        p += page_size;
        length -= page_size;
    }
    delete[] verify_buffer;
    printf("\n");
	IOWR_ALTERA_AVALON_PIO_DATA(PIO_3_BASE, 0);
    return retval;
}

int doReadFlash(uint32_t address, void *buffer, int length)
{
	static int last_sector = -1;

	if (address & 0x80000000) {
		REMOTE_FLASHSEL_1;
	} else {
		REMOTE_FLASHSEL_0;
	}
	REMOTE_FLASHSELCK_0;
	REMOTE_FLASHSELCK_1;

	address &= 0x7FFFFFFF;

	Flash *flash = get_flash();
	flash->read_linear_addr(address, length, buffer);
	return 0;
}

int protectFlashes(void)
{
	REMOTE_FLASHSEL_0;
	REMOTE_FLASHSELCK_0;
	REMOTE_FLASHSELCK_1;
	Flash *flash = get_flash();
	flash->protect_enable();

	REMOTE_FLASHSEL_1;
	REMOTE_FLASHSELCK_0;
	REMOTE_FLASHSELCK_1;

	flash = get_flash();
	flash->protect_enable();
	return 0;
}
}
