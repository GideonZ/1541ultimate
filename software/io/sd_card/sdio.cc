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

void sdio_send_command(BYTE cmd, WORD paramx, WORD paramy)
{
    SDIO_DATA = 0xff;

    SDIO_CRC  = 0; // clear CRC
    SDIO_DATA = 0x40 | cmd;
	SDIO_DATA = (BYTE)(paramx >> 8); /* MSB of parameter x */
	SDIO_DATA = (BYTE)(paramx);      /* LSB of parameter x */
	SDIO_DATA = (BYTE)(paramy >> 8); /* MSB of parameter y */
	SDIO_DATA = (BYTE)(paramy);      /* LSB of parameter y */
    SDIO_DATA = SDIO_CRC;
}

void sdio_set_speed(int speed)
{
    SDIO_SPEED = (BYTE)speed;
}

void sdio_read_block(BYTE *buf)
{
    DWORD *pul, ul;
    ul = (DWORD)buf;
    pul = (DWORD *)buf;
    if((ul & 3)==0) {
		for(int i=0;i<128;i++) {
			*(pul++) = SDIO_DATA_32;
		}
    } else {
		for(int i=0;i<512;i++) {
			*(buf++) = SDIO_DATA;
		}
    }
}

void sdio_write_block(const BYTE *buf)
{
    DWORD *pul, ul;
    ul = (DWORD)buf;
    pul = (DWORD *)buf;

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

    int time_out = 50000;

    while(SDIO_DATA != 0xFF) { // readback
    	--time_out;
		if(!time_out)
			break; // error!
	}

}
