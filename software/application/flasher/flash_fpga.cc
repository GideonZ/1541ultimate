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

extern uint32_t _ultimate_recovery_rbf_start;
extern uint32_t _ultimate_recovery_rbf_end;

extern uint32_t _ultimate_run_rbf_start;
extern uint32_t _ultimate_run_rbf_end;

extern uint32_t _free_rtos_demo_app_start;
extern uint32_t _free_rtos_demo_app_end;

extern uint32_t _ultimate_app_start;
extern uint32_t _ultimate_app_end;

uint8_t swapped_bytes[256];
uint8_t readbuf[512];

int main(int argc, char *argv[])
{
	printf("*** Flash Programmer ***\n\n");

	REMOTE_FLASHSEL_0;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;
	Flash *flash = get_flash();

    GenericHost *host = 0;
    Stream *stream = new Stream_UART;

    /*
    C64 *c64 = new C64;
    c64->reset();

    if (c64->exists()) {
    	host = c64;
    } else {
    	host = new HostStream(stream);
    }
*/
	host = new HostStream(stream);
	host->take_ownership(NULL);
    Screen *screen = host->getScreen();
//    screen->clear();

	console_print(screen, "Flash = %p. Capabilities = %8x\n", flash, getFpgaCapabilities());

    for(int i=0;i<256;i++) {
    	uint8_t temp = (uint8_t)i;
    	uint8_t swapped = 0;
    	for(int j=0; j<8; j++) {
    		swapped <<= 1;
    		swapped |= (temp & 1);
    		temp >>= 1;
    	}
    	swapped_bytes[i] = swapped;
    }

    uint8_t *data = (uint8_t *)&_ultimate_recovery_rbf_start;
	int length = (int)&_ultimate_recovery_rbf_end - (int)data;

	console_print(screen, "Swapping %d bytes from %p\n", length, data);
	for(int i=0;i < length; i++, data++) {
    	*data = swapped_bytes[*data];
    }

    data = (uint8_t *)&_ultimate_run_rbf_start;
    length = (int)&_ultimate_run_rbf_end - (int)data;

    console_print(screen, "Swapping %d bytes from %p\n", length, data);
    for(int i=0;i < length; i++, data++) {
        *data = swapped_bytes[*data];
    }

    flash->read_linear_addr(0xC0000, 512, readbuf);
    dump_hex_relative(readbuf, 512);

    flash->protect_disable();

	flash_buffer_at(flash, screen, 0x00000, false, &_ultimate_recovery_rbf_start,   &_ultimate_recovery_rbf_end,   "V1.0", "FPGA Loader");
	flash_buffer_at(flash, screen, 0xC0000, false, &_free_rtos_demo_app_start,  &_free_rtos_demo_app_end,  "V1.0", "Demo Application");

    REMOTE_FLASHSEL_1;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;

    Flash *flash2 = get_flash();
    flash_buffer_at(flash2, screen, 0x00000, false, &_ultimate_run_rbf_start,   &_ultimate_run_rbf_end,   "V1.0", "Runtime FPGA");
    flash_buffer_at(flash2, screen, 0xC0000, false, &_ultimate_app_start,  &_ultimate_app_end,  "V1.0", "Ultimate Application");

	//	console_print(screen, "\nConfiguring Flash write protection..\n");
//	flash->protect_configure();
//	flash->protect_enable();
	console_print(screen, "Done!                            \n");

	REMOTE_FLASHSEL_0;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;
    REMOTE_RECONFIG = 0xBE;
	console_print(screen, "You shouldn't see this!\n");
}
