#include "u2p.h"
#include "itu.h"
#include "iomap.h"
#include <stdio.h>

#define DDR2_TESTLOC0  (*(volatile uint32_t *)(0x0010))
#define DDR2_TESTLOC1  (*(volatile uint32_t *)(0x0014))
#define DDR2_TESTLOC2  (*(volatile uint32_t *)(0x0018))

#define MR_BL4 0x0232
#define MR_BL8 0x0233

#define OCD_ENABLE  0x4380
#define NO_DQSN     0x4400
#define RTT_50      0x4044
#define RTT_75      0x4004
#define RTT_150     0x4040
#define AL_0        0x4000
#define AL_1        0x4008
#define AL_2        0x4010
#define AL_3        0x4018
#define AL_4        0x4020
#define HALF_DRIVE  0x4002
#define DLL_DISABLE 0x4001
#define EMR    (NO_DQSN | RTT_150 | AL_2 )//| HALF_DRIVE)
#define EMROCD (EMR | OCD_ENABLE)

#define EMR2   0x8000 // no extended refresh
#define EMR3   0xC000 // all bits reserved
#define DLLRST 0x0100 // MR DLL RESET

#define TESTVALUE1  0xFF00FF00
#define TESTVALUE2  0x00FF00FF
#define TESTVALUE3  0x12345678

#define DELAY_STEPS 128

const uint8_t hexchars[] = "0123456789ABCDEF";


int try_mode(int mode);
int coarse_calibration(void);
int ram_test(void);

void ddr2_calibrate()
{
    outbyte('@');
    for (int i=0;i<1000;i++)
        ;
    LATTICE_DDR2_ENABLE    = 7;

    outbyte('#');
    for (int i=0;i<1000;i++)
        ;

    LATTICE_DDR2_ADDR_LOW  = 0x00;
    LATTICE_DDR2_ADDR_HIGH = 0x04; // A10 = all banks
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    LATTICE_DDR2_ADDR_LOW  = (EMR2 & 0xFF);
    LATTICE_DDR2_ADDR_HIGH = (EMR2 >> 8);
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR_LOW  = (EMR3 & 0xFF);
    LATTICE_DDR2_ADDR_HIGH = (EMR3 >> 8);
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR_LOW  = (EMR & 0xFF);
    LATTICE_DDR2_ADDR_HIGH = (EMR >> 8);
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR_LOW  = (DLLRST & 0xFF);
    LATTICE_DDR2_ADDR_HIGH = (DLLRST >> 8);
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR_LOW  = 0x00;
    LATTICE_DDR2_ADDR_HIGH = 0x04; // A10 = all banks
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    LATTICE_DDR2_COMMAND   = 1; // Refresh
    LATTICE_DDR2_COMMAND   = 1; // Refresh
    LATTICE_DDR2_COMMAND   = 1; // Refresh
    LATTICE_DDR2_COMMAND   = 1; // Refresh

    LATTICE_DDR2_ADDR_LOW  = (MR_BL8 & 0xFF);
    LATTICE_DDR2_ADDR_HIGH = (MR_BL8 >> 8);
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR_LOW  = (EMROCD & 0xFF);
    LATTICE_DDR2_ADDR_HIGH = (EMROCD >> 8);
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR_LOW  = (EMR & 0xFF);
    LATTICE_DDR2_ADDR_HIGH = (EMR >> 8);
    LATTICE_DDR2_COMMAND   = 0; // write MR

    for (int i=0;i<1000;i++) {

    }
    outbyte('$');

    if (coarse_calibration()) {
        while (ram_test()) {
            //return;
        }
    }

    outbyte('\n');
    outbyte(':');
    outbyte('-');
    outbyte('{');
    while(1) {
        __asm__("nop");
        __asm__("nop");
        __asm__("nop");
    }
}
#if 0
int try_mode(int mode)
{
    int phase, rep, good;
    int last_begin, best_pos, best_length;
    int state, total_good;

    outbyte('%');

    DDR2_TESTLOC0 = TESTVALUE1;
    DDR2_TESTLOC1 = TESTVALUE2;
    DDR2_TESTLOC2 = TESTVALUE3;
    
    outbyte('W');

    // Select one of the 32 possible modes and boot up DLL 
    LATTICE_DDR2_READDELAY = (mode & 3);
    outbyte('1');
    LATTICE_DDR2_DELAYSTEP = 0x0C; // Reset delays
    outbyte('2');
    LATTICE_DDR2_PHYCTRL   = 2;
    outbyte('3');
    LATTICE_DDR2_PHYCTRL   = (mode & 0x1C) << 2;
    outbyte('4');
    LATTICE_DDR2_DELAYDIR  = 1; 
    state = 0;
    best_pos = -1;
    best_length = 0;

    outbyte('\n');
    outbyte(mode + '0');
    outbyte(':');
    
    total_good = 0;
    for (phase = 0; phase < DELAY_STEPS; phase ++) {
        good = 0;
        for (rep = 0; rep < 5; rep ++) {
            if (DDR2_TESTLOC0 == TESTVALUE1)
                good++;
            if (DDR2_TESTLOC1 == TESTVALUE2)
                good++;
            if (DDR2_TESTLOC2 == TESTVALUE3)
                good++;
        }
        LATTICE_DDR2_DELAYSTEP = 0x01; // Move 
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

    if (best_pos < 0) {
        return 0;
    }
    if (best_length < 8) {
        return 0;
    }

    //printf("Chosen: Mode = %d, Pos = %d. Window = %d ps\n\r", best_mode, final_pos, 100 * best_overall);
    LATTICE_DDR2_DELAYSTEP = 0x0C; // Reset delays
    for (phase = 0; phase < best_pos; phase ++) {
        LATTICE_DDR2_DELAYSTEP = 0x01; // Step
    }
    outbyte('\n');
    outbyte(hexchars[mode & 15]);
    outbyte(hexchars[best_pos >> 4]);
    outbyte(hexchars[best_pos & 15]);
    outbyte('\r');
    outbyte('\n');
    return 1;
}
#endif

void hexbyte(uint8_t val)
{
    outbyte(hexchars[(val >> 4)  & 15]);
    outbyte(hexchars[(val >> 0)  & 15]);
}

void hexword(uint32_t val)
{
    hexbyte(val >> 24);
    hexbyte(val >> 16);
    hexbyte(val >> 8);
    hexbyte(val);
    outbyte(' ');
}

int ram_test(void)
{
    int errors = 0;
    static int run = 0;
    LATTICE_DDR2_ENABLE = 5; // Refresh On, Clock On, ODT off
//    LATTICE_DDR2_PHYCTRL = 4; // set freeze = 1
    outbyte('A' + run);
    run++;

    puts("Write");
    uint16_t *mem16 = (uint16_t *)0x10000;
    uint32_t i;
    for(i=0;i<65536;i++) {
        mem16[i] = (uint16_t)i;
    }
    puts("Read");
    LATTICE_DDR2_VALIDCNT = 0;
    for(i=0;i<65536;i++) {
        __asm__("nop");
        if (mem16[i] != (uint16_t)i)
            errors++;
    }

    if (errors) {
        puts("RAM error.");
        hexword(errors);
        hexbyte(LATTICE_DDR2_VALIDCNT);
        outbyte('\n');
        volatile uint32_t *mem32 = (uint32_t *)0x10000;
        hexword(mem32[0]);
        hexword(mem32[1]);
        hexword(mem32[2]);
        hexword(mem32[3]);
        outbyte('\n');
/*

        mem32[0] = 0x12345678;
        mem32[1] = 0x87654321;
        mem32[2] = 0xDEADBABE;
        mem32[3] = 0xABCDEF01;

        hexword(mem32[0]);                
        hexword(mem32[0]);                
        LATTICE_DDR2_PHYCTRL = 0x10;
        hexword(mem32[0]);                
        LATTICE_DDR2_PHYCTRL = 0x20;
        hexword(mem32[0]);                
        LATTICE_DDR2_PHYCTRL = 0x30;
        hexword(mem32[0]);                
        LATTICE_DDR2_PHYCTRL = 0x00;
        hexword(mem32[0]);                

        while(!ioRead8(ITU_BUTTON_REG)) {
            mem32[4] = 0x11204081; // bit 7, 6, 5, 4 occur sequentually, and bit 0 follows the 1001 pattern.
        }

        // LATTICE_DDR2_PHYCTRL |= 0x80; // Issue reset without changing settings
        puts("Going to read now!");

        uint32_t sum = 1;
        while(sum) {
            //LATTICE_DDR2_PHYCTRL = 0x10; // manual read to sync fifos
            sum += mem32[0]; 
            sum += mem32[1]; 
            sum += mem32[2]; 
//            sum += mem32[3]; 
        }
*/
        return 0;
    }
    puts("RAM OK!!");
    return 1;
}

#define BURSTS 120
int detect_bursts(void)
{
    int det = 0;
    LATTICE_DDR2_VALIDCNT = 0;

    // Test for 128 reads whether the burst detect was set
    for (int i=0;i<BURSTS;i++) {
        LATTICE_DDR2_COMMAND = 5; // Read, burst length 8
        __asm__("nop");
        __asm__("nop");
        __asm__("nop");
        if (LATTICE_DDR2_BURSTDET) {
            det++;
        }
    }
    outbyte(hexchars[det >> 4]);
    outbyte(hexchars[det & 15]);
    outbyte('/');
    uint8_t valids = LATTICE_DDR2_VALIDCNT;
    outbyte(hexchars[valids >> 4]);
    outbyte(hexchars[valids & 15]);
    outbyte(' ');
    return det;
}

int coarse_calibration()
{
    LATTICE_DDR2_DELAYSTEP = 0x00; // turn on automatic updates

    LATTICE_DDR2_READDELAY = 0x11; // RD=2, SEL=0
    LATTICE_DDR2_PHYCTRL = 1; // force update

    // Terminate by setting the burst length to 4 for normal operation
    LATTICE_DDR2_ADDR_LOW  = (MR_BL4 & 0xFF);
    LATTICE_DDR2_ADDR_HIGH = (MR_BL4 >> 8);
    LATTICE_DDR2_COMMAND   = 0; // write MR

    // Precharge all banks
    LATTICE_DDR2_ADDR_LOW  = 0x00;
    LATTICE_DDR2_ADDR_HIGH = 0x04; // A10 = all banks
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    return 1; // ignore the rest
/*
    LATTICE_DDR2_ENABLE = 1; // Refresh off
    LATTICE_DDR2_ADDR_HIGH = 0;
    LATTICE_DDR2_ADDR_LOW = 0;
    LATTICE_DDR2_COMMAND = 3; // open row 0 in bank 0

    // Select one of the 32 possible modes and boot up DLL 

    uint8_t ok = 0, good_rd = 0, good_sel = 0;
    outbyte('\n');
        for (int rd = 0; rd < 4; rd++) {
        outbyte('0' + rd);
        outbyte(':');
        for (int clksel = 0; clksel < 8; clksel++) {
            LATTICE_DDR2_PHYCTRL = 2; // pause
            LATTICE_DDR2_READDELAY = (rd << 3) | clksel;
            LATTICE_DDR2_PHYCTRL = 0; // unpause
            LATTICE_DDR2_PHYCTRL = 1; // update done

            int det = detect_bursts();

            if ((det == BURSTS) && ((clksel&3) == 2) && (rd == 2) && (!ok)) {
                ok = 1;
                good_sel = clksel;
                good_rd = rd;
            }
        }
        outbyte('\n');
    }

    good_rd  = 3;
    good_sel = 0;

    outbyte('0' + good_sel);
    outbyte('0' + good_rd);
    outbyte(':');
    LATTICE_DDR2_PHYCTRL = 2; // pause
    LATTICE_DDR2_READDELAY = (good_rd << 3) | good_sel;
    LATTICE_DDR2_PHYCTRL = 0; // unpause
    LATTICE_DDR2_PHYCTRL = 1; // update done

    // Precharge all banks
    LATTICE_DDR2_ADDR_LOW  = 0x00;
    LATTICE_DDR2_ADDR_HIGH = 0x04; // A10 = all banks
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    LATTICE_DDR2_DELAYSTEP = 0x0C; // turn off automatic updates from now on.

//    return 1;
    // Terminate by setting the burst length to 4 for normal operation
    LATTICE_DDR2_ADDR_LOW  = (MR_BL4 & 0xFF);
    LATTICE_DDR2_ADDR_HIGH = (MR_BL4 >> 8);
    LATTICE_DDR2_COMMAND   = 0; // write MR

    // Precharge all banks
    LATTICE_DDR2_ADDR_LOW  = 0x00;
    LATTICE_DDR2_ADDR_HIGH = 0x04; // A10 = all banks
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    // Open Row
    LATTICE_DDR2_ADDR_HIGH = 0;
    LATTICE_DDR2_ADDR_LOW = 0;
    LATTICE_DDR2_COMMAND = 3; // open row 0 in bank 0

    detect_bursts();

    // Precharge all banks
    LATTICE_DDR2_ADDR_LOW  = 0x00;
    LATTICE_DDR2_ADDR_HIGH = 0x04; // A10 = all banks
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    return 1;
*/
}
