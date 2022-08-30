#include "u2p.h"
#include "itu.h"
#include "iomap.h"
#include <stdio.h>

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

#define CLOCKPIN 1
#define ODT      2
#define REFRESH  4
#define CKE      8

#define EMR2   0x8000 // no extended refresh
#define EMR3   0xC000 // all bits reserved
#define DLLRST 0x0100 // MR DLL RESET

#define VERBOSE 1
#define RAM_TEST_REPORT 20

const uint8_t hexchars[] = "0123456789ABCDEF";

int coarse_calibration(void);
int ram_test(void);

void my_puts(const char *str)
{
    while(*str) {
        outbyte(*(str++));
    }
}

void hexbyte(uint8_t val)
{
    outbyte(hexchars[(val >> 4)  & 15]);
    outbyte(hexchars[(val >> 0)  & 15]);
}

void hex16(uint16_t val, const char *postfix)
{
    hexbyte(val >> 8);
    hexbyte(val);
    my_puts(postfix);
}

void hexword(uint32_t val)
{
    hexbyte(val >> 24);
    hexbyte(val >> 16);
    hexbyte(val >> 8);
    hexbyte(val);
    outbyte(' ');
}

static void init_mode_regs()
{
    LATTICE_DDR2_ENABLE    = CLOCKPIN | CKE; // Make sure refresh is off

    LATTICE_DDR2_ADDR      = 0x0400;
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    LATTICE_DDR2_ADDR      = EMR2;
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR      = EMR3;
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR      = EMR;
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR      = MR_BL8 | DLLRST;
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR      = 0x0400;
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    LATTICE_DDR2_COMMAND   = 1; // Refresh
    LATTICE_DDR2_COMMAND   = 1; // Refresh
    LATTICE_DDR2_COMMAND   = 1; // Refresh
    LATTICE_DDR2_COMMAND   = 1; // Refresh

    LATTICE_DDR2_ADDR      = MR_BL8;
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR      = EMR;
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR      = EMROCD; // OCD Defaults = 111
    LATTICE_DDR2_COMMAND   = 0; // write MR

    LATTICE_DDR2_ADDR      = EMR; // OCD EXIT = 000
    LATTICE_DDR2_COMMAND   = 0; // write MR

    for (int i=0;i<1000;i++) {

    }
}

static void reset_toggle()
{
    outbyte('#');
    for(int i=0;i<200;i++) {
        LATTICE_CLK_PHASERESET = 1;
        if (LATTICE_CLK_PHASECHECK) {
            hexbyte(i);
            outbyte('\n');
            break;
        }
    }
}

static void show_phase_sys()
{
    my_puts("\nSysClock vs CtrlClock:\n");
    LATTICE_PLL_SELECT = 1; // CLKOS2 (sys clock)
    for(int i=0;i<96;i++) {
        for(int j=0;j<200;j++)
            ;
        hexbyte(LATTICE_PLL_MEASURE2);
        outbyte(' ');
        LATTICE_PLL_PULSE = 1;
    }
    outbyte('\n');
}

static uint16_t get_latency_count()
{
    uint16_t count = ((uint16_t)U2PIO_VALUE3) << 8;
    count |= U2PIO_VALUE2;
    return count;
}

static void move_sys_clock()
{
    LATTICE_PLL_SELECT = 1; // CLKOS2 (sys clock)
    uint8_t prev,cur = 0x80;
    for(int i=0;i<96;i++) {
        prev = cur;
        for(int j=0;j<200;j++)
            ;
        cur = LATTICE_PLL_MEASURE2;
        if ((prev < 0x80) && (cur >= 0x80)) {
            break;
        }
        LATTICE_PLL_PULSE = 1;
    }
    LATTICE_PLL_SELECT = 5; // reverse CLKOS2 (sys clock)
    LATTICE_PLL_PULSE = 1;
    LATTICE_PLL_PULSE = 1;
}


void ddr2_calibrate()
{
    // Turn on clock and let DDR stabilize
    LATTICE_DDR2_ENABLE    = CLOCKPIN;
    for (int i=0;i<5000;i++)
        ;
    LATTICE_DDR2_ENABLE    = CLOCKPIN | CKE;


#if NO_BOOT
    volatile uint32_t *mem32 = (uint32_t *)0x10000;

    init_mode_regs();
    outbyte('$');
    outbyte('$');
    outbyte('$');
    uint8_t sys=0;
    uint16_t start, stop;
    do {
        int u = uart_get_byte(1000);
        switch(u) {
            case -2:
                outbyte('*');
                break;
            case '@':
                show_phase_sys();
                move_sys_clock();
                show_phase_sys();
                coarse_calibration();
                reset_toggle();
                ram_test();
                break;
            case '!':
                return; // boot!
            case 'a':
                show_phase_sys();
                break;
            case 'b':
                move_sys_clock();
                break;
            case 'c':
                coarse_calibration();
                break;
            case 'r':
                reset_toggle();
                ram_test();
                break;
            case 'R':
                while(uart_get_byte(0) < 0) {
                    ram_test();
                }
                break;
            case 't':
                reset_toggle();
                break;
            case 'l':
                start = get_latency_count();
                for(int j=0;j<256;j++) {
                    uint32_t test = mem32[0];
                }
                stop = get_latency_count();
                hexbyte(U2PIO_VALUE1);
                outbyte('-');
                hex16(stop-start, "\n");
                break;
            case 's':
                LATTICE_DDR2_PHYCTRL = 0x40; // send reset to sync module
                break;
            case 'p':
                LATTICE_DDR2_PHYCTRL = 0x20; // send reset to pll
                break;
            case 'z':
                LATTICE_PLL_SELECT = 5;
                LATTICE_PLL_PULSE = 1;
                for(int j=0;j<1000;j++)
                    ;
                hexbyte(--sys);
                break;
            case 'x':
                LATTICE_PLL_SELECT = 1;
                LATTICE_PLL_PULSE = 1;
                for(int j=0;j<1000;j++)
                    ;
                hexbyte(++sys);
                break;
            default:
                my_puts("Unknown ");
                outbyte(u);
                break;
        }
    } while(1);
#else
    move_sys_clock();
    if (coarse_calibration()) {
        if (ram_test()) {
            return;
        }
    }
    while(1) {
        __asm__("nop");
        __asm__("nop");
    }
#endif
}


int ram_test(void)
{
    // Make sure Refresh is now ON
    LATTICE_DDR2_ENABLE  = CLOCKPIN | CKE | REFRESH | ODT;

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
    uint16_t modifier = (run & 1) ? 0xFFFF : 0x0000;
    uint32_t i;
    for(i=0;i<65536;i++) {
        mem16[i] = (uint16_t)(i ^ modifier);
    }
    //my_puts("Read..");
    // LATTICE_DDR2_VALIDCNT = 0;
    for(i=0;i<65536;i++) {
        uint16_t expected = (uint16_t)(i ^ modifier);
        if (mem16[i] != expected) {
            if (errors < RAM_TEST_REPORT) {
                hex16(errors,":");
                hex16(expected, " != ");
                hex16(mem16[i], "\n");
            }
            errors++;
        }
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
    LATTICE_DDR2_ADDR      = MR_BL4;
    LATTICE_DDR2_COMMAND   = 0; // write MR

    // Precharge all banks
    LATTICE_DDR2_ADDR      = 0x0400; // A10 = all banks
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    return 1; // ignore the rest
#else
    LATTICE_DDR2_ADDR    = 0;
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
                if (ok) {
                    break;
                }
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
    LATTICE_DDR2_ADDR      = 0x0400; // A10 = all banks
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    //set_timing(2,2);
    if (ok) {
        set_timing(good_rd, good_sel);
    }

    // Terminate by setting the burst length to 4 for normal operation
    LATTICE_DDR2_ADDR      = MR_BL4;
    LATTICE_DDR2_COMMAND   = 0; // write MR

    // Precharge all banks
    LATTICE_DDR2_ADDR      = 0x0400; // A10 = all banks
    LATTICE_DDR2_COMMAND   = 2; // Precharge

    return ok;
#endif
}
