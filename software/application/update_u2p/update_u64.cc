/*
 * update.cc (for Ultimate-64)
 *
 *      Author: gideon
 */
#define HTML_DIRECTORY "/flash/html"

#include "update_common.h"
#include "checksums.h"

extern uint32_t _u64_rbf_start;
extern uint32_t _u64_rbf_end;

extern uint32_t _ultimate_app_start;
extern uint32_t _ultimate_app_end;

extern uint8_t _1581_bin_start;
extern uint8_t _1571_bin_start;
extern uint8_t _1541_bin_start;
extern uint8_t _snds1541_bin_start;
extern uint8_t _snds1571_bin_start;
extern uint8_t _snds1581_bin_start;
extern const char _index_html_start[];
extern const char _index_html_end[1];

const char *getBoardRevision(void)
{
	uint8_t rev = (U2PIO_BOARDREV >> 3);

	switch (rev) {
	case 0x10:
		return "U64 Prototype";
	case 0x11:
		return "U64 V1.1 (Null Series)";
	case 0x12:
		return "U64 V1.2 (Mass Prod)";
	case 0x13:
	    return "U64 V1.3 (Elite)";
    case 0x14:
        return "U64 V1.4 (Std/Elite)";
	}
	return "Unknown";
}

void do_update(void)
{
    setup("\033\025** Ultimate 64 Updater **\n\033\037");

    Flash *flash2 = get_flash();
    console_print(screen, "\033\024Detected Flash: %s\n", flash2->get_type_string());

    const char *fpgaType = (getFpgaCapabilities() & CAPAB_FPGA_TYPE) ? "5CEBA4" : "5CEBA2";
    console_print(screen, "Detected FPGA Type: %s.\nBoard Revision: %s\n\033\037\n", fpgaType, getBoardRevision());

    /* Extra check on the loaded images */
    const char *check_error = "\033\022\nBAD...\n\nNot flashing.\n";
    const char *check_ok = "\033\025OK!\n\033\037";

    console_print(screen, "\033\027Checking checksums of loaded images..\n");

    console_print(screen, "\033\037Checksum of FPGA image:   ");
    if(calc_checksum((uint8_t *)&_u64_rbf_start, (uint8_t *)&_u64_rbf_end) == CHK_u64_swp) {
        console_print(screen, check_ok);
    } else {
        console_print(screen, check_error);
        while(1);
    }
    console_print(screen, "\033\037Checksum of Application:  ");
    if(calc_checksum((uint8_t *)&_ultimate_app_start, (uint8_t *)&_ultimate_app_end) == CHK_ultimate_app) {
        console_print(screen, check_ok);
    } else {
        console_print(screen, check_error);
        while(1);
    }

    check_flash_disk();

    if(user_interface->popup("About to update. Continue?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {

        clear_field();
        create_dir(ROMS_DIRECTORY);
        create_dir(CARTS_DIRECTORY);
        create_dir(HTML_DIRECTORY);

        if(original_kernal_found(flash2, 0x488000)) {
            copy_flash_binary(flash2, 0x488000, 0x2000, "kernal.bin");
            copy_flash_binary(flash2, 0x48A000, 0x2000, "basic.bin");
            copy_flash_binary(flash2, 0x486000, 0x1000, "chars.bin");
        } else if (original_kernal_found(flash2, 0x458000)) {
            copy_flash_binary(flash2, 0x458000, 0x2000, "kernal.bin");
            copy_flash_binary(flash2, 0x45A000, 0x2000, "basic.bin");
            copy_flash_binary(flash2, 0x456000, 0x1000, "chars.bin");
        }

        write_flash_file("1581.rom", &_1581_bin_start, 0x8000);
        write_flash_file("1571.rom", &_1571_bin_start, 0x8000);
        write_flash_file("1541.rom", &_1541_bin_start, 0x4000);
        write_flash_file("snds1541.bin", &_snds1541_bin_start, 0xC000);
        write_flash_file("snds1571.bin", &_snds1571_bin_start, 0xC000);
        write_flash_file("snds1581.bin", &_snds1581_bin_start, 0xC000);

        write_html_file("index.html", _index_html_start, (int)_index_html_end - (int)_index_html_start);

        flash2->protect_disable();
        flash_buffer_at(flash2, screen, 0x000000, false, &_u64_rbf_start, &_u64_rbf_end,   "V1.0", "Runtime FPGA");
        flash_buffer_at(flash2, screen, 0x290000, false, &_ultimate_app_start,  &_ultimate_app_end,  "V1.0", "Ultimate Application");

        write_protect(flash2);
    }

    reset_config(flash2);
    turn_off();
}

extern "C" int ultimate_main(int argc, char *argv[])
{
	do_update();
}
