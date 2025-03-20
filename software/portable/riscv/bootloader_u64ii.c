#include "u2p.h"
#include "itu.h"
#include "iomap.h"
#include <stdio.h>
#include "u64.h"
#include "u2p.h"
#include "hw_i2c.h"
#include "mdio.h"

#define W25Q_ContinuousArrayRead_LowFrequency       0x03

#define SPI_FLASH_DATA     *((volatile uint8_t *)(FLASH_BASE + 0x00))
#define SPI_FLASH_DATA_32  *((volatile uint32_t*)(FLASH_BASE + 0x00))
#define SPI_FLASH_CTRL     *((volatile uint8_t *)(FLASH_BASE + 0x08))

#define SPI_FORCE_SS 0x01
#define SPI_LEVEL_SS 0x02

#define BOOT_MAGIC_LOCATION *((volatile uint32_t*)0x0000FFFC)
#define BOOT_MAGIC_JUMPADDR *((volatile uint32_t*)0x0000FFF8)
#define BOOT_MAGIC_VALUE    (0x1571BABE)

// Copy from i2c_drv.h, because we don't have C++ here
#define I2C_CHANNEL_HDMI 0
#define I2C_CHANNEL_1V8  1
#define I2C_CHANNEL_3V3  2

void jump_run(uint32_t a)
{
    void (*function)();
    uint32_t *dp = (uint32_t *)&function;
    *dp = a;
    function();
}

#define APPL "application\n"

const uint8_t hexchars[] = "0123456789ABCDEF";

void hexbyte(const uint8_t val)
{
    outbyte(hexchars[(val >> 4)  & 15]);
    outbyte(hexchars[(val >> 0)  & 15]);
}

void hexword(const uint32_t val)
{
    uint8_t *p = (uint8_t *)&val;
    hexbyte(p[3]);
    hexbyte(p[2]);
    hexbyte(p[1]);
    hexbyte(p[0]);
    outbyte(' ');
}

void my_puts(const char *str)
{
    while(*str) {
        outbyte(*(str++));
    }
}

static void test_access(const uint32_t addr, const uint32_t pattern)
{
    volatile uint32_t *dw = (volatile uint32_t *)addr;
    volatile uint16_t *w  = (volatile uint16_t *)addr;
    volatile uint8_t  *b  = (volatile uint8_t *)addr;

    *dw = pattern;
    hexword(*dw);
    hexword(b[0]);
    hexword(b[1]);
    hexword(b[2]);
    hexword(b[3]);
    hexword(w[0]);
    hexword(w[1]);
    b[1] = 0xAA;
    hexword(*dw);
    w[1] = 0x6969;
    hexword(*dw);
    my_puts(".\n");
}

#define MAX_APPL_SIZE 0x180000
void initializeDDR2(void);

static void init_ext_pll()
{
// PAL Color from 24 MHz: 57000505cf54534a
// HDMI Color from 54.18867 MHz: 570001010fa2caad
    const uint8_t addr = 0xC8;
    const uint8_t init_pal[] = { 0x57, 0x00, 0x05, 0x05, 0xCF, 0x54, 0x53, 0x4A };
    const uint8_t init_ntsc[] = { 0x57, 0x00, 0x03, 0x03, 0x02, 0xD0, 0x22, 0x08 };
    const uint8_t init_hdmi_50[] = { 0x57, 0x00, 0x01, 0x01, 0x0F, 0xA2, 0xCA, 0xAD };
    const uint8_t init_hdmi_60[] = { 0x57, 0x00, 0x01, 0x01, 0x55, 0xf7, 0x82, 0x89 };

    volatile t_hw_i2c *i2c_regs = (volatile t_hw_i2c *)(U64II_HW_I2C_BASE);
    hexbyte(i2c_regs->soft_reset);
    outbyte('\n');

    uint8_t hw_version = (U2PIO_BOARDREV >> 3);

    i2c_regs->channel = (hw_version == 0x15) ? I2C_CHANNEL_HDMI : I2C_CHANNEL_1V8;
    i2c_regs->data_out = addr; // Auto start
    i2c_spin_busy(i2c_regs);
    i2c_regs->data_out = 0x14; // Address
    i2c_spin_busy(i2c_regs);
    i2c_regs->data_out = 0x08; // Length
    i2c_spin_busy(i2c_regs);
    for(int i=0; i < 8; i++) {
        i2c_regs->data_out = init_pal[i];
        i2c_spin_busy(i2c_regs);
    }
    i2c_regs->stop = 1;
    i2c_spin_busy(i2c_regs);

    i2c_regs->channel = (hw_version == 0x15) ? I2C_CHANNEL_1V8 : I2C_CHANNEL_HDMI;
    i2c_regs->data_out = addr; // Auto start
    i2c_spin_busy(i2c_regs);
    i2c_regs->data_out = 0x14; // Address
    i2c_spin_busy(i2c_regs);
    i2c_regs->data_out = 0x08; // Length
    i2c_spin_busy(i2c_regs);
    for(int i=0; i < 8; i++) {
        i2c_regs->data_out = init_hdmi_50[i];
        i2c_spin_busy(i2c_regs);
    }
    i2c_regs->stop = 1;
    i2c_spin_busy(i2c_regs);
    wait_ms(2);
}

extern uint32_t *__warm_boot;
int main()
{
    puts("Hello world, U64-II!");
    if (*__warm_boot == 0) {
        *__warm_boot = 1;
        wait_ms(2);
        // either one, we reset; only one will respond
        mdio_write(0x00, 0x9100, 0);
        mdio_write(0x00, 0x9100, 3);
    }

    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    SPI_FLASH_DATA = 0xFF;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = 0xFF;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high

    uint32_t capabilities = getFpgaCapabilities();
    hexword(capabilities);
    hexbyte(U2PIO_BOARDREV);

	if (capabilities & CAPAB_SIMULATION) {
        init_ext_pll();
        goto skip;
		// jump_run(0x30000);
	}

    volatile t_hw_i2c *i2c_regs = (volatile t_hw_i2c *)(U64II_HW_I2C_BASE);
    i2c_regs->scan_enable = 0; // Turn off kb scan
    
    initializeDDR2();
    init_ext_pll();
    
    if(BOOT_MAGIC_LOCATION == BOOT_MAGIC_VALUE) {
        my_puts("Magic!\n");
        BOOT_MAGIC_LOCATION = 0;
        jump_run(BOOT_MAGIC_JUMPADDR);
    } else if (!(capabilities & CAPAB_BOOT_FPGA)) {
        uint32_t flash_addr = 0x220000; // 2176K from start. FPGA image is (uncompressed) 2141K
        
        SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
        SPI_FLASH_DATA = W25Q_ContinuousArrayRead_LowFrequency;
        SPI_FLASH_DATA = (uint8_t)(flash_addr >> 16);
        SPI_FLASH_DATA = (uint8_t)(flash_addr >> 8);
        SPI_FLASH_DATA = (uint8_t)(flash_addr);

        uint32_t *dest   = (uint32_t *)SPI_FLASH_DATA_32;
        int      length  = (int)SPI_FLASH_DATA_32;
        uint32_t run_address = SPI_FLASH_DATA_32;

        hexword((uint32_t)dest);
        hexword((uint32_t)length);
        hexword(run_address);

        my_puts(APPL);

        if(length != -1) {
            while ((length > 0) && (length < MAX_APPL_SIZE)) {
                *(dest++) = SPI_FLASH_DATA_32;
                length -= 4;
            }
            SPI_FLASH_CTRL = 0; // reset SPI chip select to idle
            my_puts("Running\n");
            uint8_t buttons = ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS;
            if ((buttons & ITU_BUTTON2) == 0) {  // right button not pressed
                jump_run(run_address);
            } else {
                my_puts("Lock\n");
            }
            while(1) {
                __asm__("nop");
                __asm__("nop");
            }
        }
    }
    my_puts("Empty; waiting for JTAG image to boot.\n");
skip:
    while(1) {
        if(BOOT_MAGIC_LOCATION == BOOT_MAGIC_VALUE) {
            my_puts("Magic: ");
            BOOT_MAGIC_LOCATION = 0;
            uint32_t addr = BOOT_MAGIC_JUMPADDR;
            hexword(addr);
            my_puts("\n");
            jump_run(addr);
        } else {
            __asm__("nop");
            //hexword(BOOT_MAGIC_LOCATION);
        }
    }
    return 0;
}
