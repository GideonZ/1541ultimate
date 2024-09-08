#include <stdint.h>
#include <stdio.h>
#include "u2p.h"

void hexbyte(const uint8_t val);
void outbyte(int c);

// Number of delay taps in an idelay.
#define DELAY_TAPS 32
#define DQS_VALUE 0x55

typedef struct {
    uint16_t addr;
    uint8_t cmd;
    uint8_t delay_updown;
    uint8_t delay_sel_data_0;
    uint8_t delay_sel_data_1;
    uint8_t delay_sel_dqs;
    uint8_t bitslip;
    uint8_t read_delay;
    uint8_t dqs_invert;
    uint8_t dummy10, dummy11;
    uint8_t control; 
} t_ddr_regs;

//#define DDR2_BASE 0x10000000

#define DDR_REGS     ((volatile t_ddr_regs *)DDR2_BASE)
#define ID_READBACK  *((volatile uint32_t *)(DDR2_BASE + 0))
#define DQS_READBACK *((volatile uint32_t *)(DDR2_BASE + 4))

#define CTRL_CLOCK_ENABLE    1
#define CTRL_ODT_ENABLE      2
#define CTRL_REFRESH_ENABLE  4
#define CTRL_CKE_ENABLE      8

#define RAM_TEST_REPORT 20
#define TEST_WORD_COUNT 65536

void hex16(uint16_t val, const char *postfix)
{
    hexbyte(val >> 8);
    hexbyte(val);
    puts(postfix);
}

static int ram_test(void)
{
    int errors = 0;
    static int run = 0;
    hexword(run);
    run++;

    puts("Write..");
    uint16_t *mem16 = (uint16_t *)0x10000;
    uint16_t modifier = (run & 1) ? 0xFFFF : 0x55AA;
    uint32_t i;
    for(i=0;i<TEST_WORD_COUNT;i++) {
        mem16[i] = (uint16_t)(i ^ modifier);
    }

    puts("Read..");
    for(i=0;i<TEST_WORD_COUNT;i++) {
        uint16_t expected = (uint16_t)(i ^ modifier);
        uint16_t read = mem16[i];
        if (read != expected) {
            if (errors < RAM_TEST_REPORT) {
                hex16(errors,":");
                hex16(expected, " != ");
                hex16(read, "\n");
            }
            errors++;
        }
    }

    if (errors) {
        puts("RAM error.\n");
        hexword(errors);
        outbyte('\n');
        volatile uint32_t *mem32 = (uint32_t *)0x10000;
        hexword(mem32[0]);
        hexword(mem32[1]);
        hexword(mem32[2]);
        outbyte('\n');
        return 0;
    }
    puts("RAM OK!!\r");
    return 1;
}


static void findDQSDelaySingle(uint8_t *dqs, int x, int *first, int *last)
{
    *first = -1;
    *last = -1;

    for (int i = x; i < DELAY_TAPS; i++) {
        if (dqs[4*i] == DQS_VALUE) {
            *first = i;
            *last = i;
            break;
        }
    }

    if (*first != -1) {
        for (int i = *first; i < DELAY_TAPS; i++) {
            if (dqs[4*i] == DQS_VALUE) {
                *last = i;
            } else {
                break;
            }
        }
    }
}

static int findDQSDelay(uint8_t *dqs)
{
    int x = 0;
    int longest = 0;
    int first = -1;
    int last = -1;
    int left, right;

    while(1) {
        findDQSDelaySingle(dqs, x, &left, &right);  
        if (left < 0) {
            break;
        }
        if (right - left > longest) {
            first = left;
            last = right;
            longest = right - left;
        }
        x = right + 1;
    }
    if (first < 0) {
        return -1;
    }

    outbyte('F');
    hexbyte(first);
    outbyte('L');
    hexbyte(last);
    int choice = -1;

    if (first == 0 && last > 9) {
        choice = last - 9;
    } else if (last == 31 && first < 23) {
        choice = first + 9;
    } else if ((last - first) > 10) {
        choice = (first + last) / 2;
    }
    outbyte('C');
    hexbyte(choice);
    outbyte('\n');
    return choice;    
}


static void calibrateDQS(void)
{
    int bs, rd, dly;
    uint32_t dqs[DELAY_TAPS];
    uint8_t *dqsb = (uint8_t *)dqs;
    volatile uint32_t *mem = (volatile uint32_t *)0x00000100;
    volatile uint32_t memval;

    mem[0] = 0x12345678;
    mem[1] = 0x87654321;

    DDR_REGS->delay_updown = 1;

    outbyte('\n');
    for (bs = 0; bs < 4; bs++) {
        for (rd = 0; rd < DELAY_TAPS; rd++) {
            memval = *mem; // Dummy read to trigger DQS
            dqs[rd] = DQS_READBACK;
            DDR_REGS->delay_sel_dqs = 0x03;
            DDR_REGS->delay_sel_data_0 = 0xFF;
            DDR_REGS->delay_sel_data_1 = 0xFF;
            // hexword(dqs[rd]);
        }
        // outbyte('\n');

        for (dly = 0; dly < 4; dly++) {
            int pos = findDQSDelay(dqsb + dly);
            if (pos > 0) {
                DDR_REGS->read_delay = dly;
                for(int i=0; i < pos; i++) {
                    DDR_REGS->delay_sel_dqs = 0x03;
                    DDR_REGS->delay_sel_data_0 = 0xFF;
                    DDR_REGS->delay_sel_data_1 = 0xFF;
                }
                outbyte('B');
                hexbyte(bs);
                outbyte('D');
                hexbyte(dly);
                outbyte('P');
                hexbyte(pos);
                outbyte('\n');
                return;
            }
        }
        DDR_REGS->bitslip = 1;
    }
    puts("Calibration failed");
}

static void ddr2_command(uint16_t addr, uint8_t cmd)
{
    DDR_REGS->addr = addr;
    DDR_REGS->cmd = cmd;

    for(int j=0;j<10;j++) {
        DDR_REGS->cmd;  // dummy read
    }
}

void initializeDDR2(void)
{
    // Enable clock and clock enable
    DDR_REGS->control = CTRL_CLOCK_ENABLE | CTRL_CKE_ENABLE;

    for (int i=0;i<2000;i++) // 200 us delay
        DDR_REGS->control;  // dummy read

    ddr2_command(0x0400, 0x02); // Issue a precharge all command
    ddr2_command(0xC000, 0x00); // Write EMR3 to zero
    ddr2_command(0x8000, 0x00); // Write EMR2 to zero
    ddr2_command(0x405A, 0x00); // Write EMR to additive latency 3
    ddr2_command(0x0742, 0x00); // Write MR to CAS 4, BL 4, DLL Reset, Write Recovery 4
    ddr2_command(0x0400, 0x02); // Issue a precharge all command

    // Issue a refresh commands
    ddr2_command(0x0400, 0x1);
    ddr2_command(0x0400, 0x1);
    ddr2_command(0x0400, 0x1);
    ddr2_command(0x0400, 0x1);

    // Prototype only
    DDR_REGS->dqs_invert = 0x01;

    calibrateDQS();

    // Turn on refresh
    DDR_REGS->control = CTRL_CLOCK_ENABLE | CTRL_REFRESH_ENABLE | CTRL_CKE_ENABLE;

    // Test
    ram_test();
}
