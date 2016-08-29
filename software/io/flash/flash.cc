#include "flash.h"
#include "at45_flash.h"
#include "at49_flash.h"
#include "w25q_flash.h"
#include "s25fl_flash.h"
#include "itu.h"
#include <stdio.h>
#include <string.h>

Flash *get_flash(void)
{
#if RUNS_ON_PC
	return new Flash(); // on PC, return stubbed class
#endif

	IndexedList<Flash*> *types = Flash :: getSupportedFlashTypes();
	for(int i=0; i < types->get_elements(); i++) {
		Flash *f = (*types)[i];
		if (f->tester()) {
			return f;
		}
	}

	return new Flash(); // stubbed base class, just never return 0!
}

int Flash :: write_image(int id, uint8_t *buffer, int length)
{
	t_flash_address flash_address;
	get_image_addresses(id, &flash_address);
	if (flash_address.id != id) {
		return -1; // ID not found
	}
	if (flash_address.max_length < length) {
		return -2; // buffer does not fit in designated area
	}

	bool do_erase = need_erase();
	int sector;
	int last_sector = -1;
    int page_size = get_page_size();
    int page = flash_address.start / page_size;
    uint8_t *p = buffer;
    uint8_t *verify_buffer = new uint8_t[page_size];
    int return_code = 0;

    while(length > 0) {
		if (do_erase) {
			sector = page_to_sector(page);
			if (sector != last_sector) {
				last_sector = sector;
				if(!erase_sector(sector)) {
					return_code = -3; // sector erase error
					break;
				}
			}
		}
        int retry = 3;
        while(retry > 0) {
            retry --;
            if(!write_page(page, p)) {
                continue;
            }
            read_page(page, verify_buffer);
            if(memcmp(verify_buffer, p, page_size)) {
                continue;
            }
            retry = -2;
        }
        if(retry != -2) {
        	return_code = -4; // programming failed
        	break;
        }
        page ++;
        p += page_size;
        length -= page_size;
    }
    delete verify_buffer;
    return return_code;
}
