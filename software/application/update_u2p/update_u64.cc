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

extern uint32_t _u64_rbf_start;
extern uint32_t _u64_rbf_end;

extern uint32_t _ultimate_app_start;
extern uint32_t _ultimate_app_end;

extern uint32_t _rom_pack_start;
extern uint32_t _rom_pack_end;

/*
extern uint32_t _ultimate_recovery_rbf_start;
extern uint32_t _ultimate_recovery_rbf_end;

extern uint32_t _recovery_app_start;
extern uint32_t _recovery_app_end;
*/

void do_update(void)
{
	printf("*** U64 Updater ***\n\n");

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
    console_print(screen, "Detected FPGA Type: %s.\nBoard Revision: %b\n\033\037\n", fpgaType, U2PIO_BOARDREV >> 3);

/*
    uint8_t was;
    uint8_t value = S25FLxxxL_Flash :: write_config_register(&was);
    console_print(screen, "Value of CR3NV was: %b and has become: %b\n", was, value);
*/

/*    int size = (int)&_u64_rbf_end - (int)&_u64_rbf_start;
    uint8_t *read_buffer = malloc(size);
    if (!read_buffer) {
    	console_print(screen, "Cannot allocate read buffer.\n");
    	while (1)
    		;
    }

    flash2->read_image(FLASH_ID_BOOTFPGA, read_buffer, size);

    uint32_t *pula = (uint32_t *)&_u64_rbf_start;
    uint32_t *pulb = (uint32_t *)read_buffer;
    size >>= 2;
    int errors = 0;
    int offset = 0;
    while(size--) {
    	if(pula[offset] != pulb[offset]) {
            if (!errors)
            	console_print(screen, "ERR: %8x: %8x != %8x.\n", offset * 4, pula[offset], pulb[offset]);
            errors++;
        }
    	offset++;
    }
    console_print(screen, "Verify errors: %d\n", errors);
*/
    if(user_interface->popup("About to flash. Continue?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
        flash2->protect_disable();
        flash_buffer_at(flash2, screen, 0x000000, false, &_u64_rbf_start, &_u64_rbf_end,   "V1.0", "Runtime FPGA");
        flash_buffer_at(flash2, screen, 0x290000, false, &_ultimate_app_start,  &_ultimate_app_end,  "V1.0", "Ultimate Application");
        flash_buffer_at(flash2, screen, 0x380000, false, &_rom_pack_start, &_rom_pack_end, "V0.0", "ROMs Pack");

    	console_print(screen, "\nConfiguring Flash write protection..\n");
    	flash2->protect_configure();
    	flash2->protect_enable();
    	console_print(screen, "Done!                            \n");
    }

/*
    if(user_interface->popup("Flash Recovery?", BUTTON_YES | BUTTON_NO) == BUTTON_YES) {
    	REMOTE_FLASHSEL_0;
        REMOTE_FLASHSELCK_0;
        REMOTE_FLASHSELCK_1;

        Flash *flash1 = get_flash();
        flash1->protect_disable();
        flash_buffer_at(flash1, screen, 0x000000, false, &_ultimate_recovery_rbf_start,   &_ultimate_recovery_rbf_end,   "V1.0", "Recovery FPGA");
        flash_buffer_at(flash1, screen, 0x080000, false, &_recovery_app_start,  &_recovery_app_end,  "V1.0", "Recovery Application");

    	console_print(screen, "\nConfiguring Flash write protection..\n");
    	console_print(screen, "Done!                            \n");
    }
*/

    wait_ms(2000);
    REMOTE_RECONFIG = 0xBE;
	console_print(screen, "You shouldn't see this!\n");
}

extern "C" int ultimate_main(int argc, char *argv[])
{
	do_update();
}
