#include "u2p.h"
#include "itu.h"
#include "iomap.h"
#include <stdio.h>

#define DDR2_TESTLOC0  (*(volatile uint32_t *)(0x0014))
#define DDR2_TESTLOC1  (*(volatile uint32_t *)(0x0018))

#define MR     0x0232
#define EMROCD 0x4780 // 01 0 0 0 1 (no DQSn) 111 (do OCD) 1 (150ohm) 000 (no AL) 0 (150 ohm) 0 (half drive) 0 (dll used)
#define EMR    0x4442 // 01 0 0 0 1 (no DQSn) 000 (no OCD) 1 (150ohm) 000 (no AL) 0 (150 ohm) 0 (half drive) 0 (dll used)
#define EMR2   0x8000 // no extended refresh
#define EMR3   0xC000 // all bits reserved
#define DLLRST 0x0100 // MR DLL RESET

#define TESTVALUE1  0x55AA6699
#define TESTVALUE2  0x12345678

const uint8_t hexchars[] = "0123456789ABCDEF";

#define CLK_DIVIDER 9
#define VCO_PHASE_STEPS 8
#define PHASE_STEPS (CLK_DIVIDER * VCO_PHASE_STEPS)
#define MOVE_READ_CLOCK 0x33
//#define MOVE_MEASURE_CLOCK 0x34


int try_mode(int mode);

void ddr2_calibrate()
{
    outbyte('@');
    DDR2_ENABLE    = 1;
    int i;
    for (i=0;i<1000;i++)
        ;

    DDR2_ADDR_LOW  = 0x00;
    DDR2_ADDR_HIGH = 0x04;
    DDR2_COMMAND   = 2; // Precharge all

    DDR2_ADDR_LOW  = (EMR2 & 0xFF);
    DDR2_ADDR_HIGH = (EMR2 >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = (EMR3 & 0xFF);
    DDR2_ADDR_HIGH = (EMR3 >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = (EMROCD & 0xFF);
    DDR2_ADDR_HIGH = (EMROCD >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = (DLLRST & 0xFF);
    DDR2_ADDR_HIGH = (DLLRST >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = 0x00;
    DDR2_ADDR_HIGH = 0x04;
    DDR2_COMMAND   = 2; // Precharge all

    DDR2_COMMAND   = 1; // Refresh
    DDR2_COMMAND   = 1; // Refresh
    DDR2_COMMAND   = 1; // Refresh
    DDR2_COMMAND   = 1; // Refresh

    DDR2_ADDR_LOW  = (MR & 0xFF);
    DDR2_ADDR_HIGH = (MR >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = (EMR & 0xFF);
    DDR2_ADDR_HIGH = (EMR >> 8);
    DDR2_COMMAND   = 0; // write MR

    for (int i=0;i<1000;i++) {

    }

    while(1) {
        if (try_mode(2)) {
            return;
        }
        if (try_mode(0)) {
            return;
        }
        if (try_mode(2)) {
            return;
        }
        if (try_mode(0)) {
            return;
        }
        if (try_mode(1)) {
            return;
        }
        if (try_mode(3)) {
            return;
        }
    }
}

int try_mode(int mode)
{
    int phase, rep, good;
    int last_begin, best_pos, best_length;
    int state;

    uint32_t testvalue1 = TESTVALUE1;
    uint32_t testvalue2 = TESTVALUE2;

    outbyte('\n');
    outbyte(mode + '0');

    DDR2_TESTLOC0 = testvalue1;
    DDR2_TESTLOC1 = testvalue2;
    DDR2_READMODE = mode;
    state = 0;
    best_pos = -1;
    best_length = 0;

    for (phase = 0; phase < (2 * PHASE_STEPS); phase ++) { // 720 degrees
        good = 0;
//          hexword(DDR2_TESTLOC0);
        for (rep = 0; rep < 7; rep ++) {
            if (DDR2_TESTLOC0 == testvalue1)
                good++;
            if (DDR2_TESTLOC1 == testvalue2)
                good++;
        }
        DDR2_PLLPHASE = MOVE_READ_CLOCK;
        outbyte(hexchars[good]);

        if ((state == 0) && (good >= 13)) {
            last_begin = phase;
            state = 1;
        } else if ((state == 1) && (good < 13)) {
//              printf("(%d:%d:%d)", last_begin, phase, phase - last_begin);
            state = 0;
            if ((phase - last_begin) > best_length) {
                best_length = phase - last_begin;
                best_pos = last_begin + (best_length >> 1);
            }
        }
    }
/*
        }
        if (testvalue1 & 0x80000000) testvalue1 = (testvalue1 << 1) | 1; else testvalue1 <<= 1;
        if (testvalue2 & 0x80000000) testvalue2 = (testvalue2 << 1) | 1; else testvalue2 <<= 1;
    }
*/
    if (best_pos < 0) {
        return 0;
    }
    if (best_length < 8) {
        return 0;
    }

    //printf("Chosen: Mode = %d, Pos = %d. Window = %d ps\n\r", best_mode, final_pos, 100 * best_overall);
    for (phase = 0; phase < best_pos; phase ++) {
        DDR2_PLLPHASE = MOVE_READ_CLOCK;
    }
    outbyte('\n');
    outbyte(hexchars[mode & 15]);
    outbyte(hexchars[best_pos >> 4]);
    outbyte(hexchars[best_pos & 15]);
    outbyte('\r');
    outbyte('\n');

    uint16_t *mem16 = (uint16_t *)256;
    for(int i=0;i<65536;i++) {
        mem16[i] = (uint16_t)i;
    }
    int errors = 0;
    for(int i=0;i<65536;i++) {
        if (mem16[i] != (uint16_t)i)
            errors++;
    }
    if (errors) {
        puts("RAM error.");
        return 0;
    }
    return 1;
}
