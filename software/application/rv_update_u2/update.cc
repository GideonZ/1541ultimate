/*
 * update.cc (for Ultimate-II)
 *
 *  Created on: May 19, 2016
 *      Author: gideon
 */

#include "../update_u2p/update_common.h"

extern uint8_t _ultimate_bin_start;
extern uint8_t _ultimate_bin_end;
extern uint8_t _rv700dd_b_start;
extern uint8_t _rv700dd_b_end;
extern uint8_t _rv700au_b_start;
extern uint8_t _rv700au_b_end;
extern uint8_t _boot2_bin_start;
extern uint8_t _boot2_bin_end;

extern uint8_t _1541_bin_start;
extern uint8_t _snds1541_bin_start;
extern const char _index_html_start[];
extern const char _index_html_end[1];

#include "checksums.h"

void do_update(void)
{
    Flash *flash2 = get_flash();
    flash2->protect_disable();

    setup("\033\025** 1541 Ultimate II Updater **\n\033\037");
    console_print(screen, "\eJFlash Type: %s\e\037\n", flash2->get_type_string());

    const char *names[] = { " Audio ", " Dual Drive ", " Cancel " };
    int choice = user_interface->popup("Flash " APPL_VERSION "?", 3, names, "adc");
    if(choice != 4) {
        check_flash_disk();
        clear_field();
        create_dir(ROMS_DIRECTORY);
        create_dir(CARTS_DIRECTORY);
        create_dir(HTML_DIRECTORY);
        write_flash_file("1541.rom", &_1541_bin_start, 0x4000);
        write_flash_file("snds1541.bin", &_snds1541_bin_start, 0xC000);
        write_html_file("index.html", _index_html_start, (int)_index_html_end - (int)_index_html_start);

        if (choice == 1) {
            flash_buffer(flash2, screen, FLASH_ID_BOOTFPGA, &_rv700au_b_start, &_rv700au_b_end, "", "Audio FPGA w/ RiscV CPU");
        } else {
            flash_buffer(flash2, screen, FLASH_ID_BOOTFPGA, &_rv700dd_b_start, &_rv700dd_b_end, "", "Dual Drive FPGA w/ RiscV");
        }
        flash_buffer(flash2, screen, FLASH_ID_BOOTAPP,  &_boot2_bin_start,  &_boot2_bin_end,  BOOT_VERSION, "Secondary bootloader");
        flash_buffer(flash2, screen, FLASH_ID_APPL,     &_ultimate_bin_start,  &_ultimate_bin_end,  APPL_VERSION, "Ultimate Application");

        write_protect(flash2, 1024);
    }
    turn_off();
}

extern "C" int ultimate_main(int argc, char *argv[])
{
	do_update();
    return 0;
}
