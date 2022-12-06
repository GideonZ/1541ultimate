/*
 * update.cc (for Ultimate-II+L)
 *
 *  Created on: September 5, 2022
 *      Author: gideon
 */

#include "update_common.h"

extern uint32_t _u2p_ecp5_impl1_bit_start;
extern uint32_t _u2p_ecp5_impl1_bit_end;

extern uint32_t _ultimate_app_start;
extern uint32_t _ultimate_app_end;

extern uint8_t _1541_bin_start;
extern uint8_t _1571_bin_start;
extern uint8_t _1581_bin_start;
extern uint8_t _snds1541_bin_start;
extern uint8_t _snds1571_bin_start;
extern uint8_t _snds1581_bin_start;

void do_update(void)
{
    setup("\033\025** Ultimate II+L Updater **\n\033\037");

    check_flash_disk();

    if(user_interface->popup("Flash Runtime?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
        clear_field();
        create_dir(ROMS_DIRECTORY);
        create_dir(CARTS_DIRECTORY);
        write_flash_file("1581.rom", &_1581_bin_start, 0x8000);
        write_flash_file("1571.rom", &_1571_bin_start, 0x8000);
        write_flash_file("1541.rom", &_1541_bin_start, 0x4000);
        write_flash_file("snds1541.bin", &_snds1541_bin_start, 0xC000);
        write_flash_file("snds1571.bin", &_snds1571_bin_start, 0xC000);
        write_flash_file("snds1581.bin", &_snds1581_bin_start, 0xC000);

        Flash *flash2 = get_flash();
        flash2->protect_disable();
        flash_buffer(flash2, screen, FLASH_ID_BOOTFPGA, &_u2p_ecp5_impl1_bit_start, &_u2p_ecp5_impl1_bit_end, "", "Runtime FPGA");
        flash_buffer(flash2, screen, FLASH_ID_APPL,     &_ultimate_app_start,     &_ultimate_app_end,  APPL_VERSION, "Ultimate Application");

        write_protect(flash2);
    }
    turn_off();
}

extern "C" int ultimate_main(int argc, char *argv[])
{
	do_update();
    return 0;
}
