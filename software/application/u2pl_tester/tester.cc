#include <stdio.h>
#include "i2c_drv.h"

#define MAXADC    0x6A
#define SEL       0x50 // 101 = Internal Reference, Always On, A11 = Analog input
#define SCAN_ONE  0x60 // 11 = Convert Selected channel, shift with 5
#define SCAN_ALL  0x00 // 00 = Scan up to Channel number 
#define CS_ONE    0x00 // Channel number, shift with 1
#define CS_ALL    0x16 // 1011 Channel number, shift with 1

int main(int argc, char **argv)
{
    printf("Hello I2C! :-)\n");

    I2C_Driver i2c;
    // i2c.i2c_scan_bus();
    // There is no secondary address, so we can only write both config AND setup bytes togher
    // (or two of the same bytes)
    i2c.i2c_write_byte(MAXADC, 0x82 | SEL, 0x01 | SCAN_ALL | CS_ALL );
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

    return 0;
}
