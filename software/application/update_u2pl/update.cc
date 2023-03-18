/*
 * update.cc (for Ultimate-II+L)
 *
 *  Created on: September 5, 2022
 *      Author: gideon
 */

#define HTML_DIRECTORY "/flash/html"
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

void do_update(void)
{
    setup("\033\025** Ultimate II+L Updater **\n\033\037");

    check_flash_disk();

    if(user_interface->popup("Flash Runtime?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
        clear_field();
        create_dir(ROMS_DIRECTORY);
        create_dir(CARTS_DIRECTORY);
        create_dir(HTML_DIRECTORY);
        write_flash_file("1581.rom", &_1581_bin_start, 0x8000);
        write_flash_file("1571.rom", &_1571_bin_start, 0x8000);
        write_flash_file("1541.rom", &_1541_bin_start, 0x4000);
        write_flash_file("snds1541.bin", &_snds1541_bin_start, 0xC000);
        write_flash_file("snds1571.bin", &_snds1571_bin_start, 0xC000);
        write_flash_file("snds1581.bin", &_snds1581_bin_start, 0xC000);
        write_html_file("index.html", sample_html, strlen(sample_html));

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
