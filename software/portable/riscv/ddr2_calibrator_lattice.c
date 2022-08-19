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

#define DELAY_STEPS 128

const uint8_t hexchars[] = "0123456789ABCDEF";


int try_mode(int mode);
int coarse_calibration(void);
int ram_test(void);

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

void my_puts(const char *str)
{
    while(*str) {
        outbyte(*(str++));
    }
}

static void init_mode_regs()
{
    LATTICE_DDR2_ENABLE    = 3; // Make sure refresh is off for now

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
}

void ddr2_calibrate()
{
    // Turn on clock and let DDR stabilize
    LATTICE_DDR2_ENABLE    = 3;

    for (int i=0;i<1000;i++)
        ;

    init_mode_regs();
    outbyte('$');

    LATTICE_PLL_SELECT = 4; // reverse direction CLKOS
    uint8_t prev,cur = 0x55;
    for(int i=0;i<50;i++) {
        prev = cur;
        cur = LATTICE_PLL_MEASURE;
        hexbyte(cur);
        outbyte(' ');
        if (!prev && cur) {
            break;
        }
        LATTICE_PLL_PULSE = 1;
        for(int j=0;j<1000;j++)
            ;
    }
    outbyte('\n');

    for(int i=0;i<46;i++) {
        hexbyte(i);
        outbyte(' ');
#if VERBOSE
        outbyte('\n');
#endif
        if (coarse_calibration()) {
            //break;
        }
        outbyte(' ');
        hexbyte(LATTICE_PLL_MEASURE);

        outbyte('\n');
        LATTICE_PLL_PULSE = 1;
    }

    // Now, pulse the phase reset for the alignment of the 50 MHz clock
    for(int i=0;i<200;i++) {
        LATTICE_CLK_PHASERESET = 1;
        if (LATTICE_CLK_PHASECHECK) {
            hexbyte(i);
            outbyte('\n');
            my_puts("Success! Let's do a RAM test.\n");
            break;
        }
    }

#if NO_BOOT > 1
    while(ram_test())
        ;
#else
    if (ram_test()) {
        return;
    }
#endif

    outbyte('\n');
    while(1) {
        __asm__("nop");
        __asm__("nop");
    }
}

int ram_test(void)
{
    // Make sure Refresh is now ON
    LATTICE_DDR2_ENABLE    = 7;

    int errors = 0;
#if NO_BOOT > 1
    static int run = 0;
    hexword(run);
    run++;
#else
    const int run = 0;
#endif

    //my_puts("Write..");
    uint16_t *mem16 = (uint16_t *)0x10000;
    uint32_t i;
    for(i=0;i<65536;i++) {
        mem16[i] = (uint16_t)(i+run);
    }
    //my_puts("Read..");
    // LATTICE_DDR2_VALIDCNT = 0;
    for(i=0;i<65536;i++) {
        if (mem16[i] != (uint16_t)(i+run))
            errors++;
    }

    if (errors) {
        my_puts("RAM error.\n");
#if NO_BOOT
        hexword(errors);
        // hexbyte(LATTICE_DDR2_VALIDCNT);
        outbyte('\n');
        volatile uint32_t *mem32 = (uint32_t *)0x10000;
        hexword(mem32[0]);
        hexword(mem32[1]);
        hexword(mem32[2]);
        outbyte('\n');
#endif
        return 0;
    }
    my_puts("RAM OK!!\r");
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
    uint8_t valids = LATTICE_DDR2_VALIDCNT;
#if VERBOSE
    hexbyte(det);
    outbyte('/');
    hexbyte(valids);
    outbyte(' ');
#endif
    return det + valids;
}

static void set_timing(int rd, int clksel)
{
//    LATTICE_DDR2_PHYCTRL = 2; // pause
    LATTICE_DDR2_READDELAY = (rd << 3) | clksel;
    LATTICE_DDR2_PHYCTRL = 0x80; // do ddr buffer reset
    LATTICE_DDR2_PHYCTRL = 0; // unpause
    LATTICE_DDR2_PHYCTRL = 1; // do update
    for(int i;i<200;i++)
        ;
}

int coarse_calibration(void)
{
    init_mode_regs();

    LATTICE_DDR2_DELAYSTEP = 0x00; // turn on automatic updates

#if 0
    set_timing(2, 2);

    // Terminate by setting the burst length to 4 for normal operation
    LATTICE_DDR2_ADDR_LOW  = (MR_BL4 & 0xFF);
    LATTICE_DDR2_ADDR_HIGH = (MR_BL4 >> 8);
    LATTICE_DDR2_COMMAND   = 0; // write MR

    // Precharge all banks
    LATTICE_DDR2_ADDR_LOW  = 0x00;
    LATTICE_DDR2_ADDR_HIGH = 0x04; // A10 = all banks
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    return 1; // ignore the rest
#else
    LATTICE_DDR2_ADDR_HIGH = 0;
    LATTICE_DDR2_ADDR_LOW = 0;
    LATTICE_DDR2_COMMAND = 3; // open row 0 in bank 0

    // Select one of the 32 possible modes and boot up DLL 

    uint8_t ok = 0;
    uint8_t good_rd = 0;
    uint8_t good_sel = 0;
#if VERBOSE
    outbyte('\n');
#endif
        for (int rd = 3; rd >= 0; rd--) {
#if VERBOSE
        outbyte('0' + rd);
        outbyte(':');
#endif
        int ok_len = 0;
        for (int clksel = 0; clksel < 8; clksel++) {
            set_timing(rd, clksel);
            int det = detect_bursts();

            if (det == 3*BURSTS) {
                ok_len ++;

                if (ok_len > 1) {
                    good_rd = rd;
                    good_sel = clksel - ((ok_len-1) >> 1);
                    ok = 1;
                }
            } else {
                ok_len = 0;
            }
        }
#if VERBOSE
        outbyte('\n');
#endif
        if (ok) {
            break;
        }
    }
    if (ok) {
        my_puts("OK");
        outbyte('0' + good_rd);
        outbyte('0' + good_sel);
    } else {
        my_puts("FAIL");
    }

    // Precharge all banks
    LATTICE_DDR2_ADDR_LOW  = 0x00;
    LATTICE_DDR2_ADDR_HIGH = 0x04; // A10 = all banks
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    //set_timing(2,2);
    if (ok) {
        set_timing(good_rd, good_sel);
    }

    // Terminate by setting the burst length to 4 for normal operation
    LATTICE_DDR2_ADDR_LOW  = (MR_BL4 & 0xFF);
    LATTICE_DDR2_ADDR_HIGH = (MR_BL4 >> 8);
    LATTICE_DDR2_COMMAND   = 0; // write MR

    // Precharge all banks
    LATTICE_DDR2_ADDR_LOW  = 0x00;
    LATTICE_DDR2_ADDR_HIGH = 0x04; // A10 = all banks
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    return ok;
#endif
}
