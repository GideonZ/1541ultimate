/*
 * prog_flash.h
 *
 *  Created on: May 19, 2016
 *      Author: gideon
 */

#ifndef PROG_FLASH_H_
#define PROG_FLASH_H_

#include "flash.h"
#include "screen.h"

bool flash_buffer(Flash *flash, Screen *screen, int id, void *buffer, void *buf_end, char *version, char *descr);
bool flash_buffer_at(Flash *flash, Screen *screen, int address, bool header, void *buffer, void *buf_end, char *version, char *descr);

#endif /* PROG_FLASH_H_ */
