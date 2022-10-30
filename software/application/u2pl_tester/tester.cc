#include <stdio.h>
#include "i2c_drv.h"

#define MAXADC         0x6A
#define SETUP          0x82
#define SU_SEL         0x50 // 101 = Internal Reference, Always On, A11 = Analog input

#define CFG_SCAN_ALL   0x00 // 00 = Scan up to Channel number 
#define CFG_SCAN_8X    0x20 // 01 = Convert Selected channel 8 times
#define CFG_SCAN_UHALF 0x40 // 10 = Scan upper half of channels
#define CFG_SCAN_ONE   0x60 // 11 = Convert Selected channel, shift with 5
#define CFG_SING_ENDED 0x01
#define CFG_CS_ONE     0x00 // Channel number, shift with 1
#define CFG_CS_ALL     0x16 // 1011 Channel number, shift with 1

int main(int argc, char **argv)
{
    printf("Hello I2C! :-)\n");

    I2C_Driver i2c;
    // i2c.i2c_scan_bus();
    // There is no secondary address, so we can only write both config AND setup bytes togher
    // (or two of the same bytes)

    uint16_t *pus;
    uint16_t all[12];
    while(1) {
        pus = (uint16_t *)0xC0;
//        i2c.i2c_write_byte(MAXADC, 0x82 | SEL, 0x01 | SCAN_ALL | CS_ALL );
//        i2c.i2c_read_raw_16(MAXADC, all, 12);

        for(int i=0;i<12;i++) {
            for (int j=0;j<2;j++) {
                i2c.i2c_write_byte(MAXADC, SETUP | SU_SEL, CFG_SING_ENDED | CFG_SCAN_8X | CFG_CS_ONE | (i << 1) );
                i2c.i2c_read_raw_16(MAXADC, &all[i], 1);
            }
        }

        for(int i=0;i<12;i++) {
            *(pus++) = all[i] & 0x03FF;
        }
        (*pus)++;
    }

/*
    uint16_t all[12];
    for(int i=0;i<12;i++) {
        i2c.i2c_read_raw_16(MAXADC, all, 12);
        printf("%04x\n", all[i]);
    }

    for(int i=0;i<12;i++) {
        i2c.i2c_write_byte(MAXADC, 0x82 | SEL, 0x01 | SCAN_ONE | CS_ONE | (i << 1) );
        uint16_t single;
        i2c.i2c_read_raw_16(MAXADC, &single, 1);
        printf("%2d: %04x\n", i, single);
    }

    for(int i=11;i>=0;i--) {
        i2c.i2c_write_byte(MAXADC, 0x82 | SEL, 0x01 | SCAN_ONE | CS_ONE | (i << 1) );
        uint16_t single;
        i2c.i2c_read_raw_16(MAXADC, &single, 1);
        printf("%2d: %04x\n", i, single);
    }
*/
    return 0;
}
