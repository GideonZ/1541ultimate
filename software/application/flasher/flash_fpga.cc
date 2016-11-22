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
#include "i2c.h"
#include "rtc.h"

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
	int test_c = 0;
	int dummy;

	ENTER_SAFE_SECTION
	i2c_write_byte(0xA2, 0x03, 0x55);
	uint8_t rambyte = i2c_read_byte(0xA2, 0x03, &dummy);
	LEAVE_SAFE_SECTION
	if (rambyte != 0x55)
		test_c ++;

	ENTER_SAFE_SECTION
	i2c_write_byte(0xA2, 0x03, 0xAA);
	rambyte = i2c_read_byte(0xA2, 0x03, &dummy);
	LEAVE_SAFE_SECTION
	if (rambyte != 0xAA)
		test_c ++;

    char date_buffer1[32];
    char time_buffer1[32];
    console_print(screen, "%s ", rtc.get_long_date(date_buffer1, 32));
	console_print(screen, "%s\n", rtc.get_time_string(time_buffer1, 32));

	if (test_c) {
		console_print(screen, "RTC does not respond (correctly)\n");
	}
	test_audio();

	if (flash->get_number_of_pages() != 4096) {
		console_print(screen, "* ERROR: Unexpected flash type Bank #0\n");
		while(1)
			;
	}
	flash->protect_disable();

	flash_buffer_at(flash, screen, 0x00000, false, &_ultimate_recovery_rbf_start,   &_ultimate_recovery_rbf_end,   "V1.0", "FPGA Loader");
	flash_buffer_at(flash, screen, 0x80000, false, &_recovery_app_start,  &_recovery_app_end,  "V1.0", "Recovery Application");

    char date_buffer2[32];
    char time_buffer2[32];
    console_print(screen, "%s ", rtc.get_long_date(date_buffer2, 32));
	console_print(screen, "%s\n", rtc.get_time_string(time_buffer2, 32));

	// more than 1 second has passed, so the time buffers shall not be equal
	if(strcmp(time_buffer1, time_buffer2) == 0)
		test_c ++;

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

	if (test_a || test_b || test_c) {
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
