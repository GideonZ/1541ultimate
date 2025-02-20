/*
 * update.cc (for Ultimate-II)
 *
 *  Created on: May 19, 2016
 *      Author: gideon
 */

#include "../update_u2p/update_common.h"

extern uint8_t _ultimate_bin_start;
extern uint8_t _ultimate_bin_end;
extern uint8_t _mb_boot_700_b_start;
extern uint8_t _mb_boot_700_b_end;
extern uint8_t _mb_boot2_bin_start;
extern uint8_t _mb_boot2_bin_end;

extern uint8_t _1541_bin_start;
extern uint8_t _snds1541_bin_start;

#include "checksums.h"

void do_update(void)
{
    Flash *flash2 = get_flash();
    flash2->protect_disable();

    setup("\033\025** 1541 Ultimate II Updater **\n\033\037");

    if(user_interface->popup("Flash " APPL_VERSION "?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {

        check_flash_disk();
        clear_field();
        create_dir(ROMS_DIRECTORY);
        create_dir(CARTS_DIRECTORY);
        write_flash_file("1541.rom", &_1541_bin_start, 0x4000);
        write_flash_file("snds1541.bin", &_snds1541_bin_start, 0xC000);

        flash_buffer(flash2, screen, FLASH_ID_BOOTFPGA, &_mb_boot_700_b_start, &_mb_boot_700_b_end, "", "Xilinx FPGA");
        flash_buffer(flash2, screen, FLASH_ID_BOOTAPP,  &_mb_boot2_bin_start,  &_mb_boot2_bin_end,  BOOT_VERSION, "Secondary bootloader");
        flash_buffer(flash2, screen, FLASH_ID_APPL,     &_ultimate_bin_start,  &_ultimate_bin_end,  APPL_VERSION, "Ultimate Application");

        write_protect(flash2, 1024);
    }
    turn_off();
}

extern "C" int ultimate_main(int argc, char *argv[])
{
	do_update();
}
