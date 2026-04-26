/*
 * chargen.h
 *
 *  Created on: Mar 10, 2018
 *      Author: gideon
 */

#ifndef CHARGEN_H_
#define CHARGEN_H_

#include <stdint.h>

typedef struct {
    uint8_t LINE_CLOCKS_HI;
    uint8_t LINE_CLOCKS_LO;
    uint8_t CHAR_WIDTH;
    uint8_t CHAR_HEIGHT;
    uint8_t CHARS_PER_LINE;
    uint8_t ACTIVE_LINES;
    uint8_t X_ON_HI;
    uint8_t X_ON_LO;
    uint8_t Y_ON_HI;
    uint8_t Y_ON_LO;
    uint8_t POINTER_HI;
    uint8_t POINTER_LO;
    uint8_t PERFORM_SYNC;
    uint8_t TRANSPARENCY;
} t_chargen_registers;

#endif /* CHARGEN_H_ */
