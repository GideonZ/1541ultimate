#include "u2p.h"
#include "itu.h"
#include "iomap.h"
#include <stdio.h>

#define DDR2_TESTLOC0  (*(volatile uint32_t *)(0x0010))
#define DDR2_TESTLOC1  (*(volatile uint32_t *)(0x0014))
#define DDR2_TESTLOC2  (*(volatile uint32_t *)(0x0018))

#define MR     0x0232
#define EMR    0x4440 // 01 0 0 0 1 (no DQSn) 000 (no OCD) 1 (150ohm) 000 (no AL) 0 (150 ohm) 0 (full drive) 0 (dll used)
#define EMROCD 0x47C0 // 01 0 0 0 1 (no DQSn) 111 (do OCD) 1 (150ohm) 000 (no AL) 0 (150 ohm) 0 (full drive) 0 (dll used)
#define EMR2   0x8000 // no extended refresh
#define EMR3   0xC000 // all bits reserved
#define DLLRST 0x0100 // MR DLL RESET

#define TESTVALUE1  0xFF00FF00
#define TESTVALUE2  0x00FF00FF
#define TESTVALUE3  0x12345678

const uint8_t hexchars[] = "0123456789ABCDEF";

#define CLK_DIVIDER 10
#define VCO_PHASE_STEPS 8
#define PHASE_STEPS (CLK_DIVIDER * VCO_PHASE_STEPS)
#define MOVE_WRITE_CLOCK 0x34
#define MOVE_READ_CLOCK 0x35
#define MOVE_MEASURE_CLOCK 0x36

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

    do {
        if (try_mode(2)) {
            return;
        }
        if (try_mode(1)) {
            return;
        }
        if (try_mode(0)) {
            return;
        }
        if (try_mode(3)) {
            return;
        }
        DDR2_PLLPHASE = MOVE_WRITE_CLOCK;
    } while(1);
//    puts("Failed to calibrate.");
    while(1);
}

int try_mode(int mode)
{
    int phase, rep, good;
    int last_begin, best_pos, best_length;
    int state, total_good;

    DDR2_TESTLOC0 = TESTVALUE1;
    DDR2_TESTLOC1 = TESTVALUE2;
    DDR2_TESTLOC2 = TESTVALUE3;
    DDR2_READMODE = mode;
    state = 0;
    best_pos = -1;
    best_length = 0;

    outbyte('\n');
    outbyte(mode + '0');
    outbyte(':');

    total_good = 0;
    for (phase = 0; phase < (2 * PHASE_STEPS); phase ++) { // 720 degrees
        good = 0;
        for (rep = 0; rep < 5; rep ++) {
            if (DDR2_TESTLOC0 == TESTVALUE1)
                good++;
            if (DDR2_TESTLOC1 == TESTVALUE2)
                good++;
            if (DDR2_TESTLOC2 == TESTVALUE3)
                good++;
        }
        DDR2_PLLPHASE = MOVE_READ_CLOCK;
        outbyte(hexchars[good]);
        total_good += good;

        if ((state == 0) && (good >= 14)) {
            last_begin = phase;
            state = 1;
        } else if ((state == 1) && (good < 14)) {
            state = 0;
            if ((phase - last_begin) > best_length) {
                best_length = phase - last_begin;
                best_pos = last_begin + (best_length >> 1);
            }
        }
    }

    if (total_good == 30 * PHASE_STEPS) {
        puts("Sim?");
        return 1;
    }
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
//    outbyte(hexchars[mode & 15]);
    outbyte(hexchars[best_pos >> 4]);
    outbyte(hexchars[best_pos & 15]);
    outbyte('\r');
    outbyte('\n');

/*    for (phase = 0; phase < 160; phase ++) {
        uint8_t measure = DDR2_MEASURE;
        if ((measure & 0x0F) > (measure >> 4)) {
            printf("+");
        } else if ((measure & 0x0F) < (measure >> 4)) {
            printf("-");
        } else {
            printf("0");
        }
        DDR2_PLLPHASE = MOVE_MEASURE_CLOCK;
    }
    printf("\n\r");
*/

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
        return 0;
    }
    return 1;
}
