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

const char *sample_html = 
"<html>\n"
"<head>\n"
"<meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n"
"</head>\n"
"<body>\n"
"<h1>Welcome to your Ultimate 64!</h1>\n"
"<p>This is a sample page to show the operation of the built-in web server.</p>\n"
"<p>This web server can serve files, but also provides an API, through the route <tt>/v1</tt>.</p>\n"
"<p>More information about this API can be found in the <a href=\"https://1541u-documentation.readthedocs.io/en/latest/\">documentation</a>.</p>\n"
"<p>Try here to load a SID file through this simple form:</p>\n"
"<form action=\"/v1/runners:sidplay\" method=\"POST\" enctype=\"multipart/form-data\">\n"
"    <input type=\"file\" id=\"myFile2\" name=\"bestand\" multiple>\n"
"    <input type=\"submit\" value=\"Send SID file to API\">\n"
"</form>\n"
"\n"
"</body>\n"
"</html>\n"
;

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

    if(user_interface->popup("About to flash. Continue?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {

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

        write_html_file("index.html", sample_html, strlen(sample_html));

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
