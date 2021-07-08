/*
 * update.cc (for Ultimate-II+)
 *
 *  Created on: May 19, 2016
 *      Author: gideon
 */

#include "prog_flash.h"
#include <stdio.h>
#include "itu.h"
#include "host.h"
#include "c64.h"
#include "screen.h"
#include "keyboard.h"
#include "stream.h"
#include "stream_uart.h"
#include "host_stream.h"
#include "dump_hex.h"
#include "u2p.h"
#include "rtc.h"
#include "userinterface.h"
#include "s25fl_l_flash.h"
#include "u64.h"
#include "checksums.h"
#include "overlay.h"
#include "filemanager.h"
#include "init_function.h"

extern uint32_t _u64_rbf_start;
extern uint32_t _u64_rbf_end;

extern uint32_t _ultimate_app_start;
extern uint32_t _ultimate_app_end;

/*
extern uint32_t _rom_pack_start;
extern uint32_t _rom_pack_end;
*/

extern uint8_t _1581_bin_start;
extern uint8_t _1571_bin_start;
extern uint8_t _1541_bin_start;
extern uint8_t _snds1541_bin_start;
extern uint8_t _snds1571_bin_start;
extern uint8_t _snds1581_bin_start;

static Screen *screen;

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

const uint8_t orig_kernal[] = {
        0x85, 0x56, 0x20, 0x0f, 0xbc, 0xa5, 0x61, 0xc9, 0x88, 0x90, 0x03, 0x20, 0xd4, 0xba, 0x20, 0xcc
};

static bool original_kernal_found(Flash *flash, int addr)
{
    uint8_t buffer[16];
    flash->read_linear_addr(addr, 16, buffer);
    return (memcmp(buffer, orig_kernal, 16) == 0);
}

static void create_dir(const char *name)
{
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->create_dir(name);
    console_print(screen, "Creating dir: %s\n", FileSystem :: get_error_string(fres));
}

static void write_flash_file(const char *name, uint8_t *data, int length)
{
    File *f;
    uint32_t dummy;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(ROMS_DIRECTORY, name, FA_CREATE_ALWAYS | FA_WRITE, &f);
    if (fres == FR_OK) {
        fres = f->write(data, length, &dummy);
        console_print(screen, "Writing %s to /flash: %s\n", name, FileSystem :: get_error_string(fres));
        fm->fclose(f);
    }
}

static void copy_flash_binary(Flash *flash, uint32_t addr, uint32_t len, const char *fn)
{
    uint8_t *buffer = new uint8_t[len];
    flash->read_linear_addr(addr, len, buffer);
    FileManager *fm = FileManager :: getFileManager();
    console_print(screen, "Saving %s to /flash..\n", fn);
    fm->save_file(false, ROMS_DIRECTORY, fn, buffer, len, NULL);
}

void do_update(void)
{
	printf("*** U64 Updater ***\n\n");

	Flash *flash = get_flash();

    GenericHost *host = 0;
    Stream *stream = new Stream_UART;

    InitFunction :: executeAll();

    C64 *c64 = C64 :: getMachine();

    if (c64->exists()) {
    	host = c64;
    } else {
    	host = new HostStream(stream);
    }
    screen = host->getScreen();

    OVERLAY_REGS->TRANSPARENCY = 0x00;

    UserInterface *user_interface = new UserInterface("\033\021** Ultimate 64 Updater **\n\033\037");
    user_interface->init(host);
    host->take_ownership(user_interface);
    user_interface->appear();
    screen->move_cursor(0, 2);

    char time_buffer[32];
    console_print(screen, "%s ", rtc.get_long_date(time_buffer, 32));
	console_print(screen, "%s\n", rtc.get_time_string(time_buffer, 32));

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

    if(user_interface->popup("About to flash. Continue?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {

        create_dir(ROMS_DIRECTORY);
        create_dir(CARTS_DIRECTORY);

        if(original_kernal_found(flash2, 0x488000)) {
            copy_flash_binary(flash2, 0x488000, 0x2000, "kernal.bin");
        } else if (original_kernal_found(flash2, 0x458000)) {
            copy_flash_binary(flash2, 0x458000, 0x2000, "kernal.bin");
        }
        copy_flash_binary(flash2, 0x48A000, 0x2000, "basic.bin");
        copy_flash_binary(flash2, 0x486000, 0x1000, "chars.bin");

        write_flash_file("1581.rom", &_1581_bin_start, 0x8000);
        write_flash_file("1571.rom", &_1571_bin_start, 0x8000);
        write_flash_file("1541.rom", &_1541_bin_start, 0x4000);
        write_flash_file("snds1541.bin", &_snds1541_bin_start, 0xC000);
        write_flash_file("snds1571.bin", &_snds1571_bin_start, 0xC000);
        write_flash_file("snds1581.bin", &_snds1581_bin_start, 0xC000);

        flash2->protect_disable();
        flash_buffer_at(flash2, screen, 0x000000, false, &_u64_rbf_start, &_u64_rbf_end,   "V1.0", "Runtime FPGA");
        flash_buffer_at(flash2, screen, 0x290000, false, &_ultimate_app_start,  &_ultimate_app_end,  "V1.0", "Ultimate Application");

    	console_print(screen, "\nConfiguring Flash write protection..\n");
    	flash2->protect_configure();
    	flash2->protect_enable();
    	console_print(screen, "Done!                            \n");
    }

    if(user_interface->popup("Reset Configuration? (Recommended)", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
        int num = flash2->get_number_of_config_pages();
        for (int i=0; i < num; i++) {
            flash2->clear_config_page(i);
        }
    }

    console_print(screen, "\n\033\022Turning OFF machine in 5 seconds....\n");

    wait_ms(5000);
    U64_POWER_REG = 0x2B;
    U64_POWER_REG = 0xB2;
    U64_POWER_REG = 0x2B;
    U64_POWER_REG = 0xB2;
    console_print(screen, "You shouldn't see this!\n");
}

extern "C" int ultimate_main(int argc, char *argv[])
{
	do_update();
}
