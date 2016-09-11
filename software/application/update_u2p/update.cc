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

extern uint32_t _ultimate_run_rbf_start;
extern uint32_t _ultimate_run_rbf_end;

extern uint32_t _ultimate_app_start;
extern uint32_t _ultimate_app_end;

extern uint32_t _rom_pack_start;
extern uint32_t _rom_pack_end;

void do_update(void)
{
	printf("*** U2+ Updater ***\n\n");

	REMOTE_FLASHSEL_1;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;
	Flash *flash = get_flash();

    GenericHost *host = 0;
    Stream *stream = new Stream_UART;

    C64 *c64 = new C64;

    if (c64->exists()) {
    	host = c64;
    } else {
    	host = new HostStream(stream);
    }
    Screen *screen = host->getScreen();

    UserInterface *user_interface = new UserInterface("\033\021** 1541 Ultimate II+ Updater **\n\033\037");
    user_interface->init(host);
    user_interface->appear();
    screen->move_cursor(0, 2);

    char time_buffer[32];
    console_print(screen, "%s ", rtc.get_long_date(time_buffer, 32));
	console_print(screen, "%s\n", rtc.get_time_string(time_buffer, 32));

	REMOTE_FLASHSEL_1;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;

    if(user_interface->popup("About to flash. Continue?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
        Flash *flash2 = get_flash();
        flash2->protect_disable();
        flash_buffer_at(flash2, screen, 0x000000, false, &_ultimate_run_rbf_start,   &_ultimate_run_rbf_end,   "V1.0", "Runtime FPGA");
        flash_buffer_at(flash2, screen, 0x0C0000, false, &_ultimate_app_start,  &_ultimate_app_end,  "V1.0", "Ultimate Application");
        flash_buffer_at(flash2, screen, 0x200000, false, &_rom_pack_start, &_rom_pack_end, "V0.0", "ROMs Pack");

    	console_print(screen, "\nConfiguring Flash write protection..\n");
    	flash->protect_configure();
    	flash->protect_enable();
    	console_print(screen, "Done!                            \n");
    	wait_ms(5000);
    }

    REMOTE_RECONFIG = 0xBE;
	console_print(screen, "You shouldn't see this!\n");
}

extern "C" int ultimate_main(int argc, char *argv[])
{
	do_update();
}
