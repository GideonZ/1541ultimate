#include "sdio.h"
extern "C" {
    #include "small_printf.h"
}

/* Function to sense the presence of the card
   and write protect, if exists */
   
int sdio_sense(void)
{
    return (int)(SDIO_SWITCH >> 2); // invert, should be changed in HW
}

void sdio_init(void)
{
    SDIO_SPEED = 254;
    SDIO_CTRL  = SPI_FORCE_SS | SPI_LEVEL_SS;
    
    for(int i=0;i<100;i++) {
        SDIO_DATA = 0xff;
    }
    
    SDIO_CTRL = 0;
}

void sdio_send_command(uint8_t cmd, WORD paramx, WORD paramy)
{
    SDIO_DATA = 0xff;

    SDIO_CRC  = 0; // clear CRC
    SDIO_DATA = 0x40 | cmd;
	SDIO_DATA = (uint8_t)(paramx >> 8); /* MSB of parameter x */
	SDIO_DATA = (uint8_t)(paramx);      /* LSB of parameter x */
	SDIO_DATA = (uint8_t)(paramy >> 8); /* MSB of parameter y */
	SDIO_DATA = (uint8_t)(paramy);      /* LSB of parameter y */
    SDIO_DATA = SDIO_CRC;
}

void sdio_set_speed(int speed)
{
    SDIO_SPEED = (uint8_t)speed;
}

uint8_t sdio_read_block(uint8_t *buf)
{
	// timeout is 100 ms max.
	// as with  write block
	int  fb_timeout = 240000;
	uint8_t b;

    while((b = SDIO_DATA) == 0xFF) {
    	--fb_timeout;
		if(!fb_timeout)
			return 0xFF; // timeout
	}
    if (b != 0xFE)
    	return b;

	uint32_t *pul, ul;
    ul = (uint32_t)buf;
    pul = (uint32_t *)buf;
    if((ul & 3)==0) {
		for(int i=0;i<128;i++) {
			*(pul++) = SDIO_DATA_32;
		}
    } else {
		for(int i=0;i<512;i++) {
			*(buf++) = SDIO_DATA;
		}
    }

	/* Checksum (2 byte) - ignore for now */
	SDIO_DATA = 0xff;
	SDIO_DATA = 0xff;

    return 0;
}

bool sdio_write_block(const uint8_t *buf)
{
    uint32_t *pul, ul;
    ul = (uint32_t)buf;
    pul = (uint32_t *)buf;

    SDIO_DATA = 0xFE; // start of block
    if((ul & 3)==0) {
		for(int i=0;i<128;i++) {
			SDIO_DATA_32 = *(pul++);
		}
    } else {
		for(int i=0;i<512;i++) {
			SDIO_DATA = *(buf++);
		}
    }
	SDIO_DATA = 0xFF; // dummy (crc, etc)
    SDIO_DATA = 0xFF;
    SDIO_DATA = 0xFF;

    // Timeout for SD writes = 250 ms (fixed by SDA).
    // Because the SPI read is in the loop below, running at 25 MHz,
    // we can estimate that each cycle of the while takes 8.5 bits * 40 ns = 360 ns
    // plus a few instructions to make the loop (let's say 6 clocks) = 120 ns
    // So the loop is approx 480 ns (minimum), if executed from cache.
    // A safe timeout value is therefore 250e6 / 480 = 520833.
    // Let's round it up to 600000.

    int time_out = 600000;

    while(SDIO_DATA != 0xFF) { // readback
    	--time_out;
		if(!time_out)
			return false; // error!
	}
    return true; // ok
}
