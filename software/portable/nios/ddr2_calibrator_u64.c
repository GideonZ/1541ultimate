#include "u2p.h"
#include "itu.h"
#include "iomap.h"
#include <stdio.h>

#define DDR2_TESTLOC0  (*(volatile uint32_t *)(0x0000))
#define DDR2_TESTLOC1  (*(volatile uint32_t *)(0x0004))

#define MR     0x0232
#define EMR    0x4440 // 01 0 0 0 1 (no DQSn) 000 (no OCD) 1 (150ohm) 000 (no AL) 0 (150 ohm) 0 (full drive) 0 (dll used)
#define EMROCD 0x47C0 // 01 0 0 0 1 (no DQSn) 111 (do OCD) 1 (150ohm) 000 (no AL) 0 (150 ohm) 0 (full drive) 0 (dll used)
#define EMR2   0x8000 // no extended refresh
#define EMR3   0xC000 // all bits reserved
#define DLLRST 0x0100 // MR DLL RESET

#define TESTVALUE1  0x55AA6699
#define TESTVALUE2  0x12345678

const uint8_t hexchars[] = "0123456789ABCDEF";

#define CLK_DIVIDER 9
#define VCO_PHASE_STEPS 8
#define PHASE_STEPS (CLK_DIVIDER * VCO_PHASE_STEPS)

void ddr2_calibrate()
{
    outbyte('@');
    DDR2_ENABLE    = 1;

    DDR2_ADDR_LOW  = 0x00;
    DDR2_ADDR_HIGH = 0x04;
    DDR2_COMMAND   = 2; // Precharge all

    DDR2_ADDR_LOW  = (EMR2 & 0xFF);
    DDR2_ADDR_HIGH = (EMR2 >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = (EMR3 & 0xFF);
    DDR2_ADDR_HIGH = (EMR3 >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = (EMR & 0xFF);
    DDR2_ADDR_HIGH = (EMR >> 8);
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

    DDR2_ADDR_LOW  = (EMROCD & 0xFF);
    DDR2_ADDR_HIGH = (EMROCD >> 8);
    DDR2_COMMAND   = 0; // write MR

    DDR2_ADDR_LOW  = (EMR & 0xFF);
    DDR2_ADDR_HIGH = (EMR >> 8);
    DDR2_COMMAND   = 0; // write MR

    for (int i=0;i<1000;i++) {

    }
    //    alt_putstr("DDR2 Initialized!\n\r"); // delay!

    uint32_t testvalue1 = TESTVALUE1;
    uint32_t testvalue2 = TESTVALUE2;

    int mode, phase, rep, good;
    int last_begin, best_pos, best_length;
    int state;
    int best_mode = -1;
    int final_pos = 0;
    int best_overall = -1;

    for (mode = 0; mode < 4; mode ++) {
        outbyte('\n');
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
            DDR2_PLLPHASE = 0x33; // move read clock
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
        if (best_length > best_overall) {
            best_overall = best_length;
            best_mode = mode;
            final_pos = best_pos;
        }
        if (testvalue1 & 0x80000000) testvalue1 = (testvalue1 << 1) | 1; else testvalue1 <<= 1;
        if (testvalue2 & 0x80000000) testvalue2 = (testvalue2 << 1) | 1; else testvalue2 <<= 1;
    }

    //printf("Chosen: Mode = %d, Pos = %d. Window = %d ps\n\r", best_mode, final_pos, 100 * best_overall);
    DDR2_READMODE = best_mode;
    for (phase = 0; phase < final_pos; phase ++) {
        DDR2_PLLPHASE = 0x33; // move read clock
    }
    outbyte('\n');
    outbyte(hexchars[best_mode & 15]);
    outbyte(hexchars[final_pos >> 4]);
    outbyte(hexchars[final_pos & 15]);
    outbyte('\r');
    outbyte('\n');

    uint16_t *mem16 = (uint16_t *)0;
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
        while(1);
    }
}

