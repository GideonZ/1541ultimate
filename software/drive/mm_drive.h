/*
 * mm_drive.h
 *
 *  Created on: Jun 10, 2021
 *      Author: gideon
 */

#ifndef MM_DRIVE_H_
#define MM_DRIVE_H_

#include <stdint.h>

typedef struct {

    uint8_t power;       // 0
    uint8_t reset;       // 1
    uint8_t hw_addr;     // 2
    uint8_t sensor;      // 3
    uint8_t inserted;    // 4
    uint8_t rammap;      // 5
    uint8_t side;        // 6
    uint8_t manual_write;// 7
    uint8_t track;       // 8
    uint8_t status;      // 9
    uint8_t mem_addr;    // 10
    uint8_t audio_addr;  // 11
    uint8_t diskchange;  // 12
    uint8_t drivetype;   // 13

} mmdrive_regs_t;

#endif /* MM_DRIVE_H_ */
