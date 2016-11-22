/*
 * flash_tester.cc
 *
 *  Created on: May 19, 2016
 *      Author: gideon
 */

#include "prog_flash.h"
#include <stdio.h>
#include "itu.h"
#include "host.h"
#include "screen.h"
#include "keyboard.h"
#include "stream.h"
#include "stream_uart.h"
#include "host_stream.h"
#include "dump_hex.h"
#include "u2p.h"

extern uint32_t _testexec_rbf_start;
extern uint32_t _testexec_rbf_end;

extern uint32_t _test_loader_app_start;
extern uint32_t _test_loader_app_end;


int main(int argc, char *argv[])
{
	printf("*** Flash Programmer To program Test Loader ***\n\n");

	REMOTE_FLASHSEL_0;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;
	Flash *flash = get_flash();

    GenericHost *host = 0;
    Stream *stream = new Stream_UART;
	host = new HostStream(stream);

	host->take_ownership(NULL);
    Screen *screen = host->getScreen();
    screen->clear();

	console_print(screen, "Flash: %p. Capabilities: %8x\n", flash, getFpgaCapabilities());

	if (flash->get_number_of_pages() != 4096) {
		console_print(screen, "* ERROR: Unexpected flash type Bank #0\n");
		while(1)
			;
	}
	flash->protect_disable();

	flash_buffer_at(flash, screen, 0x00000, false, &_testexec_rbf_start,   &_testexec_rbf_end,   "V1.0", "Test Exec FPGA");
	flash_buffer_at(flash, screen, 0x80000, false, &_test_loader_app_start,  &_test_loader_app_end,  "V1.0", "Test Loader Appl.");

	//console_print(screen, "\nConfiguring Flash write protection..\n");
	//flash->protect_configure();
	//flash->protect_enable();

	console_print(screen, "Done!                            \n");

	REMOTE_FLASHSEL_0;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;
    REMOTE_RECONFIG = 0xBE;
	console_print(screen, "You shouldn't see this!\n");
}
