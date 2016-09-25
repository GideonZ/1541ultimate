/*
 * flash_fpga.cc
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
#include "bist.h"

extern uint32_t _ultimate_recovery_rbf_start;
extern uint32_t _ultimate_recovery_rbf_end;

extern uint32_t _ultimate_run_rbf_start;
extern uint32_t _ultimate_run_rbf_end;

extern uint32_t _recovery_app_start;
extern uint32_t _recovery_app_end;

extern uint32_t _ultimate_app_start;
extern uint32_t _ultimate_app_end;

extern uint32_t _rom_pack_start;
extern uint32_t _rom_pack_end;

uint8_t swapped_bytes[256];
uint8_t readbuf[512];

extern "C" {
	void codec_init(void);
}

int main(int argc, char *argv[])
{
	printf("*** Flash Programmer ***\n\n");

	REMOTE_FLASHSEL_0;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;
	Flash *flash = get_flash();

    GenericHost *host = 0;
    Stream *stream = new Stream_UART;

    C64 *c64 = new C64;
    c64->reset();

    if (c64->exists()) {
    	host = c64;
    } else {
    	host = new HostStream(stream);
    }
	host->take_ownership(NULL);
    Screen *screen = host->getScreen();
    screen->clear();

	console_print(screen, "Flash: %p. Capabilities: %8x\n", flash, getFpgaCapabilities());

	codec_init();
	int test_a = test_iec(screen);
	int test_b = test_cartbus(screen);
	test_audio();

	if (flash->get_number_of_pages() != 4096) {
		console_print(screen, "* ERROR: Unexpected flash type Bank #0\n");
		while(1)
			;
	}
	flash->protect_disable();

	flash_buffer_at(flash, screen, 0x00000, false, &_ultimate_recovery_rbf_start,   &_ultimate_recovery_rbf_end,   "V1.0", "FPGA Loader");
	flash_buffer_at(flash, screen, 0x80000, false, &_recovery_app_start,  &_recovery_app_end,  "V1.0", "Recovery Application");

	//console_print(screen, "\nConfiguring Flash write protection..\n");
	//flash->protect_configure();
	//flash->protect_enable();

	REMOTE_FLASHSEL_1;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;

    Flash *flash2 = get_flash();

	if (flash2->get_number_of_pages() != 16384) {
		console_print(screen, "* ERROR: Unexpected flash type Bank #1\n");
		while(1)
			;
	}
    flash2->protect_disable();
    //memset(&_ultimate_run_rbf_start, 0xFF, (uint32_t)&_ultimate_run_rbf_end - (uint32_t)&_ultimate_run_rbf_start);
    //memset(&_ultimate_app_start, 0xFF, (uint32_t)&_ultimate_app_end - (uint32_t)&_ultimate_app_start);
    //memset(&_rom_pack_start, 0xFF, (uint32_t)&_rom_pack_end - (uint32_t)&_rom_pack_start);

    flash_buffer_at(flash2, screen, 0x000000, false, &_ultimate_run_rbf_start,   &_ultimate_run_rbf_end,   "V1.0", "Runtime FPGA");
    flash_buffer_at(flash2, screen, 0x0C0000, false, &_ultimate_app_start,  &_ultimate_app_end,  "V1.0", "Ultimate Application");
    flash_buffer_at(flash2, screen, 0x200000, false, &_rom_pack_start, &_rom_pack_end, "V0.0", "ROMs Pack");

	console_print(screen, "\nConfiguring Flash write protection..\n");
	flash->protect_configure();
	flash->protect_enable();
	console_print(screen, "Done!                            \n");

	if (test_a || test_b) {
		console_print(screen, "There were errors.\n");
		while(1)
			;
	}

	REMOTE_FLASHSEL_0;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;
    REMOTE_RECONFIG = 0xBE;
	console_print(screen, "You shouldn't see this!\n");
}
