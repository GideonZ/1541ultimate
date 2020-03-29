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

extern uint32_t _u2p_carttester_rbf_start;
extern uint32_t _u2p_carttester_rbf_end;

void do_update(void)
{
	printf("*** U2+ Updater ***\n\n");

	REMOTE_FLASHSEL_1;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;
	Flash *flash = get_flash();

    GenericHost *host = 0;
    Stream *stream = new Stream_UART;

    C64 *c64 = C64 :: getMachine();

    if (c64->exists()) {
    	host = c64;
    } else {
    	host = new HostStream(stream);
    }
    Screen *screen = host->getScreen();

    UserInterface *user_interface = new UserInterface("\033\021** Flasher for Tester **\n\033\037");
    user_interface->init(host);
    host->take_ownership(user_interface);
    user_interface->appear();
    screen->move_cursor(0, 2);

    char time_buffer[32];
    console_print(screen, "%s ", rtc.get_long_date(time_buffer, 32));
	console_print(screen, "%s\n", rtc.get_time_string(time_buffer, 32));

    if(user_interface->popup("Flash TesterFPGA?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
    	REMOTE_FLASHSEL_1;
        REMOTE_FLASHSELCK_0;
        REMOTE_FLASHSELCK_1;

        Flash *flash2 = get_flash();
        flash2->protect_disable();
        flash_buffer_at(flash2, screen, 0x000000, false, &_u2p_carttester_rbf_start,   &_u2p_carttester_rbf_end,   "V1.0", "Tester FPGA");

    	console_print(screen, "Done!                            \n");
    }


    wait_ms(2000);
    console_print(screen, "\nPLEASE TURN OFF YOUR MACHINE.\n");

    while (1)
        ;
}

extern "C" int ultimate_main(int argc, char *argv[])
{
	do_update();
}
