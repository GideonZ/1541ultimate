#include "hw_i2c_drv.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <math.h>
#include "u2p.h"
#include "u64.h"
#include "c64.h"
#include "dump_hex.h"
#include "flash.h"

#define PLL1 0xC8
#define PLL2 0xCA
#define HUB  0x58
#define EXP1 0x40
#define EXP2 0x42
#define AUD  0x34

#include "fpll.h"
#include "chargen.h"
#include "hdmi_scan.h"
#include <string.h>
#include "hw_i2c.h"

extern "C" {
void scan()
{
    uint8_t rd = 0xFF, wr = 0x01;
    int res;
    rd = i2c->i2c_read_byte(0x42, 0x01, &res);
    if (rd == 0xFF) {
        printf("No Key pressed\n");
        return;
    }
    for(int i=0; i<8; i++) {
        i2c->i2c_write_byte(0x42, 0x02, wr ^ 0xFF);
        rd = i2c->i2c_read_byte(0x42, 0x01, &res);
        if (rd != 0xFF) {
            printf("Col %d: %02x\n", i , rd);
        }
        wr <<= 1;
    }
    i2c->i2c_write_byte(0x42, 0x02, 0x00); // all pins on
}

void ultimate_main(void *context)
{
    puts("U64II Test\n");

    U64_CLOCK_SEL = 0x00; // PAL reference frequency
    U64_HDMI_ENABLE = 0;
    U64_HDMI_REG = U64_HDMI_DDC_ENABLE;
     
    if(i2c && 0) {
        printf("HDMI I2C:\n");
        i2c->set_channel(0);
        i2c->i2c_scan_bus();
        printf("\n");

        printf("1.8V I2C:\n");
        i2c->set_channel(1);
        i2c->i2c_scan_bus();
        printf("\n");

        printf("3.3V I2C:\n");
        i2c->set_channel(2);
        i2c->i2c_scan_bus();
        printf("\n");
    }

    printf("Measurement: %08x\n", U64_CLOCK_FREQ);
    printf("Flash: %s\n", get_flash()->get_type_string());

    SetVideoMode1080p(e_PAL_50);

    vTaskDelay(100);

    extern uint8_t _basic_901226_01_bin_start[8192];
    extern uint8_t _kernal_901227_03_bin_start[8192];
    extern uint8_t _characters_901225_01_bin_start[4096];

    memcpy((uint8_t *)U64_KERNAL_BASE,  _kernal_901227_03_bin_start, 8192);
    memcpy((uint8_t *)U64_BASIC_BASE,   _basic_901226_01_bin_start, 8192);
    memcpy((uint8_t *)U64_CHARROM_BASE, _characters_901225_01_bin_start, 4096);
    C64_MODE = C64_MODE_RESET;
    C64_STOP = 0;
    C64_MODE = C64_MODE_UNRESET;

    volatile uint32_t *palette = (volatile uint32_t *)U64II_HDMI_PALETTE;
    palette[0] = 0x00222222;

    U64II_CHARGEN_REGS->TRANSPARENCY     = 0;
    U64II_CHARGEN_REGS->CHAR_WIDTH       = 8;
    U64II_CHARGEN_REGS->CHAR_HEIGHT      = 0x80 | 16;
    U64II_CHARGEN_REGS->CHARS_PER_LINE   = 40;
    U64II_CHARGEN_REGS->ACTIVE_LINES     = 25;
    U64II_CHARGEN_REGS->X_ON_HI          = 5;
    U64II_CHARGEN_REGS->X_ON_LO          = 240;
    U64II_CHARGEN_REGS->Y_ON_HI          = 0;
    U64II_CHARGEN_REGS->Y_ON_LO          = 100;
    U64II_CHARGEN_REGS->POINTER_HI       = 0;
    U64II_CHARGEN_REGS->POINTER_LO       = 0;
    U64II_CHARGEN_REGS->PERFORM_SYNC     = 0;

    Screen_MemMappedCharMatrix *screen = new Screen_MemMappedCharMatrix((char *)U64II_CHARGEN_SCREEN_RAM, (char *)U64II_CHARGEN_COLOR_RAM, 40, 25);
    screen->clear();
    console_print(screen, "Hello world!\n");

    int res;
    i2c->set_channel(1);
    i2c->i2c_write_byte(0x40, 0x03, 0x00); // All pins output
    i2c->i2c_write_byte(0x40, 0x01, 0xF0); // Output Port
    i2c->i2c_write_byte(0x40, 0x02, 0x00); // No polarity inversion

    // Column is A  (drive), Row is B  (read)
    // Port 0,               Port 1
    i2c->i2c_write_byte(0x42, 0x06, 0x00); // All pins output on port 0
    i2c->i2c_write_byte(0x42, 0x07, 0xFF); // All pins input on port 1
    i2c->i2c_write_byte(0x42, 0x02, 0x00); // Output Port, all columns selected

    while(true) {
        vTaskDelay(400);
        U64II_CHARGEN_REGS->TRANSPARENCY     = 0x80;    
        scan();
        vTaskDelay(400);
        U64II_CHARGEN_REGS->TRANSPARENCY     = 0x00;    
        scan();
    }

    vTaskDelay(100000);
}

}
