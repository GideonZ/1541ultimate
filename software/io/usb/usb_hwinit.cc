#include "i2c_drv.h"
#include "u2p.h"
#include "usb_nano.h"
#include <stdio.h>

// Hardware Initialization
#define HUB_ADDRESS_2513 0x58
#define HUB_ADDRESS_2503 0x5A

#define VIDL    0x00
#define VIDM    0x01
#define PIDL    0x02
#define PIDM    0x03
#define DIDL    0x04
#define DIDM    0x05
#define CFG1    0x06
#define CFG2    0x07
#define CFG3    0x08
#define NRD     0x09
#define PDS     0x0A
#define PDB     0x0B
#define MAXPS   0x0C
#define MAXPB   0x0D
#define HCMCS   0x0E
#define HCMCB   0x0F
#define PWRT    0x10
#define BOOSTUP 0xF6
#define BOOST20 0xF8
#define PRTSP   0xFA
#define PRTR12  0xFB
#define STCD    0xFF

#define USB2503_STCD    0x00
#define USB2503_VIDL    0x01
#define USB2503_VIDM    0x02
#define USB2503_PIDL    0x03
#define USB2503_PIDM    0x04
#define USB2503_DIDL    0x05
#define USB2503_DIDM    0x06
#define USB2503_CFG1    0x07
#define USB2503_CFG2    0x08
#define USB2503_NRD     0x09
#define USB2503_PDS     0x0A
#define USB2503_PDB     0x0B
#define USB2503_MAXPS   0x0C
#define USB2503_MAXPB   0x0D
#define USB2503_HCMCS   0x0E
#define USB2503_HCMCB   0x0F
#define USB2503_PWRT    0x10

static uint8_t hub_registers_2503[] = {
	USB2503_VIDL,  0x24,
	USB2503_VIDM,  0x04,
	USB2503_PIDL,  0x03,
	USB2503_PIDM,  0x25,
	USB2503_DIDL,  0xA0,
	USB2503_DIDM,  0x0B,
	USB2503_CFG1,  0x97, // Self powered, no leds, hs enabled, multi tt, normal EOP, no current sense 10, ganged switch
	USB2503_CFG2,  0x20, // copied from 2513
	USB2503_NRD,   0x00, // all ports removable
	USB2503_PDS,   0x00, // all ports enabled
	USB2503_PDB,   0x00, // all ports enabled
	USB2503_MAXPS, 0x01, // 2 mA, as per spec
	USB2503_MAXPB, 0x32, // 100 mA?
	USB2503_HCMCS, 0x01, // 2 mA
	USB2503_HCMCB, 0x32, // 100 mA?
	USB2503_PWRT,  0x32, // 100 ms power on time
	USB2503_STCD,  0x03, // start!
};

static uint8_t hub_registers_2513[] = {
        0x24, 0x04, 0x13, 0x25, 0xA0, 0x0B, 0x9A, 0x20,
        0x02, 0x00, 0x00, 0x00, 0x01, 0x32, 0x01, 0x32,
        0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

#define HUB_RESET_0   U2PIO_HUB_RESET = 1
#define HUB_RESET_1   U2PIO_HUB_RESET = 0

static void USB2513Init()
{
    uint8_t status;

    puts("Configuring USB2513.");
    HUB_RESET_0;
    for(int i=0;i<50;i++) {
        (void)U2PIO_HUB_RESET;
    }
    HUB_RESET_1;
    for(int i=0;i<50;i++) {
        (void)U2PIO_HUB_RESET;
    }

    uint8_t buffer[32];
    const uint8_t reset = 0x02;
    const uint8_t start = 0x01;

    // Reset the USB hub
    if (i2c->i2c_write_block(HUB_ADDRESS_2513, STCD, &reset, 1)) {
        puts("Failed to reset the USB hub");
        return;
    }

    // Configure the USB hub
    if (i2c->i2c_write_block(HUB_ADDRESS_2513, 0, hub_registers_2513, sizeof(hub_registers_2513))) {
        puts("Failed to configure the USB hub");
        return;
    }

    // Start the USB hub with the new settings
    if (i2c->i2c_write_block(HUB_ADDRESS_2513, STCD, &start, 1)) {
        puts("Failed to start the USB hub");
        return;
    }
    puts("USB Hub successfully configured.");
}

static void USB2503Init()
{
    uint8_t status;
    puts("Configuring USB2503.");

    HUB_RESET_0;
    for(int i=0;i<50;i++) {
        (void)U2PIO_HUB_RESET;
    }
    HUB_RESET_1;
    for(int i=0;i<50;i++) {
        (void)U2PIO_HUB_RESET;
    }

    // Configure the USB hub
    for(int i=0; i<sizeof(hub_registers_2503); i+=2) {
        if (i2c->i2c_write_byte(
                HUB_ADDRESS_2503,
                hub_registers_2503[i],
                hub_registers_2503[i+1])) {
            puts("Failed to write byte to USB hub");
            return;
        }
    }
    puts("USB Hub successfully configured.");
}

void initialize_usb_hub()
{
    NANO_START = 0;
    U2PIO_ULPI_RESET = 1;
    uint16_t *dst = (uint16_t *)NANO_BASE;
    for (int i = 0; i < 2048; i += 2) {
        *(dst++) = 0;
    }
    if (!i2c) {
        puts("I2C not initialized, initialization of USB hub failed.");
        return;
    }
    i2c->set_channel(I2C_CHANNEL_3V3);
    if (i2c->i2c_probe(HUB_ADDRESS_2513)) {
        USB2513Init();
    } else if (i2c->i2c_probe(HUB_ADDRESS_2503)) {
        USB2503Init();
    } else {
        puts("No USB hub found.");
    }

    U2PIO_ULPI_RESET = 0;

    // enable buffer
    U2PIO_ULPI_RESET = U2PIO_UR_BUFFER_ENABLE;
}
