/*
 * flash_switch.cc
 *
 *  Created on: Nov 15, 2016
 *      Author: gideon
 */


#include "u2p.h"
#include <stdio.h>
#include "flash.h"
#include <string.h>

extern "C" {
int testFlashSwitch(void)
{
	REMOTE_FLASHSEL_0;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;
	Flash *flash = get_flash();
	uint32_t id0[2];
	uint32_t id1[2];
	const char *type0 = flash->get_type_string();
	flash->read_serial(id0);

	printf("Flash 0: %s %08X:%08X\n", type0, id0[0], id0[1]);

	REMOTE_FLASHSEL_1;
    REMOTE_FLASHSELCK_0;
    REMOTE_FLASHSELCK_1;
    flash = get_flash();
    const char *type1 = flash->get_type_string();
	flash->read_serial(id1);

	printf("Flash 1: %s %08X:%08X\n", type1, id1[0], id1[1]);

	if ((id0[0] == id1[0]) && (id0[1] == id1[1])) {
		return 1;
	}
	if (strcmp(type0, "W25Q80") != 0) {
		return 2;
	}
	if (strcmp(type1, "W25Q32") != 0) {
		return 3;
	}
	return 0;
}

}
