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

extern uint8_t _ultimate_altera_rbf_start;
extern uint8_t _ultimate_altera_rbf_end;

uint8_t swapped_bytes[256];

int main(int argc, char *argv[])
{
	printf("*** Primary Flash Programmer ***\n\n");
	Flash *flash = get_flash();
    printf("Flash = %p. Capabilities = %8x\n", flash, getFpgaCapabilities());

    GenericHost *host = 0;
    Stream *stream = new Stream_UART;

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
    dump_hex(swapped_bytes, 256);

    uint8_t *data = &_ultimate_altera_rbf_start;
	int length = (int)&_ultimate_altera_rbf_end - (int)data;

	printf("Swapping %d bytes\n", length);
	for(int i=0;i < length; i++, data++) {
    	*data = swapped_bytes[*data];
    }

    dump_hex(&_ultimate_altera_rbf_start, 256);
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

	flash->protect_disable();

	host->take_ownership(NULL);
    Screen *screen = host->getScreen();
//    screen->clear();

	flash_buffer_at(flash, screen, 0, false, &_ultimate_altera_rbf_start,   &_ultimate_altera_rbf_end,   "V1.0", "FPGA Loader");

	console_print(screen, "\nConfiguring Flash write protection..\n");
//	flash->protect_configure();
//	flash->protect_enable();
	console_print(screen, "Done!\n");
}
