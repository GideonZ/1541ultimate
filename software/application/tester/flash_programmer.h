/*
 * flash_programmer.h
 *
 *  Created on: Nov 17, 2016
 *      Author: gideon
 */

#ifndef FLASH_PROGRAMMER_H_
#define FLASH_PROGRAMMER_H_
#include <stdint.h>


int doFlashProgram(uint32_t address, void *buffer, int length);
int protectFlashes(void);


#endif /* FLASH_PROGRAMMER_H_ */
