/*
 *   1541 Ultimate - The Storage Solution for your Commodore computer.
 *   Copyright (C) 2009  Gideon Zweijtzer - Gideon's Logic Architectures
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Abstract:
 *	 Functions directly handling the SD-Card SPI interface.
 */
//#include "manifest.h"
#include <stdio.h>
#include "integer.h"
#include "sd_card.h"

extern "C" {
    #include "itu.h"
    #include "small_printf.h"
}


#define TXT printf
#define DBG(x) x

#define RW_SPEED  0

/* Constructor */
SdCard :: SdCard(void) : BlockDevice()
{
    sdhc = false;
    sd_type = 0;
    initialized = false;
#ifdef OS
    mutex = xSemaphoreCreateMutex();
#endif
}

/* Destructor */
SdCard :: ~SdCard(void)
{
    printf("** DESTRUCTING SDCARD BLOCKDEVICE **\n");
#ifdef OS
	vSemaphoreDelete(mutex);
#endif
}

DSTATUS SdCard :: status(void)
{
    int sense = sdio_sense();
    uint8_t status = 0;
    if(!(sense & SD_CARD_DETECT))
        status |= STA_NODISK;
    else if (sense & SD_CARD_PROTECT)
        status |= STA_PROTECT;

    if(!initialized) {
		printf("SdCard not initialized.. %p\n", this);
		status |= STA_NOINIT;
	}
    return status;        
}        

/*
-------------------------------------------------------------------------------
							init
							====
  Abstract:
	Intialize the SD-Card. This function should be called after an insertion
	event.

  Return:
	0:		No errors
	other:	Function specific error
-------------------------------------------------------------------------------
*/
DSTATUS SdCard :: init(void)
{
	int i;
	uint8_t resp;
	

    sdhc = false;
    sd_type = 0;
    initialized = false;

//    if(!(sdio_sense() & SD_CARD_DETECT))
//        return STA_NODISK;

    sdio_init(); // force SPI mode

	sdio_set_speed(200); // slllooooowww

//    printf("Reset card...");
	/* Try to send reset command up to 100 times */
	i=100;
	do {
		sdio_send_command(0, 0, 0);
		resp=Resp8b();
//		printf("%02x ", resp);
	}
	while(resp!=1 && i--);
	
//    printf("Tried %d times. %02x\n", 100-i, resp);

	if(resp!=1){
		if(resp==0xff){
			return STA_NOINIT;
		}
		else{
			Resp8bError(resp);
			return STA_NOINIT;
		}
	}

    sdio_send_command(8, 0, 0x01AA);
    resp = Resp8b();
//    printf("Response to cmd 8 = %02x.\n", resp);
    if(resp & 0x04) {
        printf("SD V1.x\n");
        sd_type = 1;
    } else {
        printf("SD V2.00\n");
        resp = SDIO_DATA;
//        printf("Cmd Version: %02x ", resp);
        SDIO_DATA = 0xff;
        resp = SDIO_DATA;
//        printf("Voltage: %02x\n", resp);
        resp = SDIO_DATA;
        if(resp != 0xAA) {
            return STA_NOINIT; // unusable card
        }
        sd_type = 2;
    }
        
    //printf("Wait for card to initialize.\n\r");
	/* Wait till card is ready initialising (returns 0 on CMD1) */
	/* Try up to 32000 times. */
	i=32000;
	do{
        sdio_send_command(CMDAPPCMD, 0, 0);
        resp=Resp8b();
        sdio_send_command(41, 0x4000, 0);
		
		resp=Resp8b();
		if((resp!=0)&&(resp!=1))
			Resp8bError(resp);
	}
	while(resp==1 && i--);
	
    //printf("Waiting for %d. %02x\n", 32000-i, resp);

	if(resp!=0){
//		Resp8bError(resp);
		return STA_NOINIT;
	}

    if(sd_type == 2) {
        sdio_send_command(CMDREADOCR, 0, 0);
        resp=Resp8b();
        Resp8bError(resp);
        resp = SDIO_DATA;
        SDIO_DATA = 0xff;
        SDIO_DATA = 0xff;
        SDIO_DATA = 0xff;
        if(resp & 0x40) {
            sdhc = true;
            printf("High capacity!\n");
        }
    }

	sdio_set_speed(RW_SPEED); 

	printf("Setting initialized to true.. %p\n", this);
    initialized = true;
/*
    error = sd_getDriveSize(&size);
    printf("error=%d. Size = %lu.\n", error, size);
*/
/*
    printf("Set Block Len.\n");
    sdio_send_command(CMDSETBLKLEN, 0, 512);
	resp=Resp8b();
    printf("error=%x\n", resp);
	Resp8bError(resp);

    printf("Get status.\n");
	sdio_send_command(CMDGETSTATUS, 0, 0);
	resp=Resp8b();
    printf("error=%x\n", resp);
	Resp8bError(resp);
*/	
	return 0;
}

/*
-------------------------------------------------------------------------------
							read
							====
  Abstract:
	Reads one or more sectors from the SD-Card

  Parameters
	address:	sector address to read from
	buf:		pointer to the buffer receiving the data to read

  Return:
	0:		no error
	other:	error
-------------------------------------------------------------------------------
*/
/* ****************************************************************************
 * WAIT ?? -- FIXME
 * CMDCMD
 * WAIT
 * CARD RESP
 * WAIT
 * DATA BLOCK IN
 * 		START BLOCK
 * 		DATA
 * 		CHKS (2B)
 */

DRESULT SdCard :: read(uint8_t* buf, uint32_t address, int sectors)
{
	uint8_t cardresp;
	uint8_t firstblock;
	uint32_t place;

//	DBG((TXT("sd_readSector::Trying to read sector %u and store it at %p.\n"),address,&buf[0]));
//    printf("Trying to read sector %d to %p.\n",address,buf);

/*
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("SdCard unavailable.\n");
    	return RES_NOTRDY;
    }
*/
    DRESULT res = RES_OK;

    for(int j=0;j<sectors;j++) {
    	place=(sdhc)?(address):(address<<9);

    	ENTER_SAFE_SECTION
    	sdio_send_command(CMDREAD, (uint16_t) (place >> 16), (uint16_t) place);
    	
    	cardresp=Resp8b(); /* Card response */ 
    
    	if (cardresp != 0x00) {
    		Resp8bError(cardresp);
    		LEAVE_SAFE_SECTION
    		res = RES_ERROR;
    		break;
    	}
    	cardresp = sdio_read_block(buf);

		if (cardresp != 0x00) {
        	Resp8bError(cardresp);
    		LEAVE_SAFE_SECTION
    		res = RES_ERROR;
    		break;
    	}
		LEAVE_SAFE_SECTION
    	
    	address ++;
    	buf += SD_SECTOR_SIZE;
    }

/*	xSemaphoreGive(mutex);*/
    return res;
}

/*
-------------------------------------------------------------------------------
							write
							=====
  Abstract:
	Writes sectors on the SD-Card

  Parameters
	address:	sector address to write to
	buf:		pointer to the buffer containing the data to write

  Return:
	0:		no error
	other:	error
-------------------------------------------------------------------------------
*/
/* ****************************************************************************
 * WAIT ?? -- FIXME
 * CMDWRITE
 * WAIT
 * CARD RESP
 * WAIT
 * DATA BLOCK OUT
 *      START BLOCK
 *      DATA
 *      CHKS (2B)
 * BUSY...
 */

DRESULT SdCard :: write(const uint8_t* buf, uint32_t address, int sectors )
{
	uint32_t place;
	uint8_t  resp;
#ifdef SD_VERIFY
    int   ver;
#endif    
    //printf("Trying to write %p to %d sectors from %d.\n",buf,sectors,address);

/*
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("SdCard unavailable.\n");
    	return RES_NOTRDY;
    }
*/
    DRESULT res = RES_OK;

    for(int j=0;j<sectors;j++) {
    	place=(sdhc)?(address):(address<<9);
    	ENTER_SAFE_SECTION
    	sdio_send_command(CMDWRITE, (uint16_t)(place >> 16), (uint16_t) place);
    
/*      // wait for 0.5 ms
        ioWrite8(ITU_TIMER, 100);
        while(ioRead8(ITU_TIMER))
            ;
*/        
    	resp = Resp8b(); /* Card response */
    	Resp8bError(resp);
    
        bool success = sdio_write_block(buf);
    	LEAVE_SAFE_SECTION
        if (!success) {
            printf("Timeout error writing block %d\n", address);
            res = RES_ERROR;
            break;
        }    
#ifdef SD_VERIFY
    	xSemaphoreGive(mutex);
        ver = verify(address, buf);
        xSemaphoreTake(mutex, 5000);
        if(ver) {
        	res = RES_ERROR;
        	break;
        }
#endif
        address ++;
    	buf += SD_SECTOR_SIZE;
    }
/*	xSemaphoreGive(mutex);*/
	return res;
}

DRESULT SdCard :: ioctl(uint8_t command, void *data)
{
    switch(command) {
        case GET_SECTOR_COUNT:
            return get_drive_size((uint32_t *)data);
            break;
        case GET_SECTOR_SIZE:
            (*(uint32_t *)data) = 512;
            break;
        case GET_BLOCK_SIZE:
            (*(uint32_t *)data) = 128*1024;
            break;
        default:
            printf("IOCTL %d.\n", command);
            return RES_PARERR;
    }
    return RES_OK;
}


/*
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
PRIVATE FUNCIONS
-------------------------------------------------------------------------------
-------------------------------------------------------------------------------
*/

/*
-------------------------------------------------------------------------------
							Resp8b
							======
  Abstract:
	Gets an 8bits response from the SD-Card

  Return:
	resp:		the 8bits response
-------------------------------------------------------------------------------
*/
uint8_t SdCard :: Resp8b(void)
{
	uint8_t i;
	uint8_t resp;
	
	/* Respone will come after 1 - 8 pings */
//    printf("Resp: ");
	for(i=0;i<8;i++){
		resp = SDIO_DATA;
//        printf("%b ", resp);
		if(resp != 0xff) {
			return(resp);
	    }
	}
	return(resp);
}

/*
-------------------------------------------------------------------------------
							Resp16b
							=======
  Abstract:
	Gets an 16bits response from the SD-Card

  Return:
	resp:		the 16bits response
-------------------------------------------------------------------------------
*/
uint16_t SdCard :: Resp16b(void)
{
	uint16_t resp;
	
	resp = ( ((uint16_t)Resp8b()) << 8 ) & 0xff00;
	resp |= SDIO_DATA;
	
	return(resp);
}

/*
-------------------------------------------------------------------------------
							Resp8bError
							===========
  Abstract:
	Displays the error message that goes with the error
	FOR DEBUG PURPOSES
	
  Parameters
	value:		error code
-------------------------------------------------------------------------------
*/
void SdCard :: Resp8bError(uint8_t value)
{
    if(value == 0xFF) {
        DBG((TXT("Unexpected IDLE byte.\n")));
        return;
    }
    if(value & 0x40)  DBG((TXT("Argument out of bounds.\n")));
    if(value & 0x20)  DBG((TXT("Address out of bounds.\n")));
    if(value & 0x10)  DBG((TXT("Error during erase sequence.\n")));
    if(value & 0x08)  DBG((TXT("CRC failed.\n")));
    if(value & 0x04)  DBG((TXT("Illegal command.\n")));
	if(value & 0x02)  DBG((TXT("Erase reset (see SanDisk docs p5-13).\n")));
    if(value & 0x01)  DBG((TXT("Card is initialising.\n")));
}

/*
-------------------------------------------------------------------------------
							get_state
							=========
  Abstract:
	Gets the state of the SD-CARD

  Return:
	1:		normal state
	-1:		error state	
-------------------------------------------------------------------------------
int SdCard :: get_state(void)
{
	WORD value;
	
	sdio_send_command(13, 0, 0);
	value=Resp16b();
	
	switch(value)
	{
		case 0x0000:
			DBG((TXT("Everything OK.\n")));
			return(1);
			break;
		case 0x0001:
			DBG((TXT("Card is Locked.\n")));
			break;
		case 0x0002:
			DBG((TXT("WP Erase Skip, Lock/Unlock Cmd Failed.\n")));
			break;
		case 0x0004:
			DBG((TXT("General / Unknown error -- card broken?.\n")));
			break;
		case 0x0008:
			DBG((TXT("Internal card controller error.\n")));
			break;
		case 0x0010:
			DBG((TXT("Card internal ECC was applied, but failed to correct the data.\n")));
			break;
		case 0x0020:
			DBG((TXT("Write protect violation.\n")));
			break;
		case 0x0040:
			DBG((TXT("An invalid selection, sectors for erase.\n")));
			break;
		case 0x0080:
			DBG((TXT("Out of Range, CSD_Overwrite.\n")));
			break;
		default:
			if(value>0x00FF)
				Resp8bError((BYTE) (value>>8));
			else
				printf("Unknown error: 0x%x (see SanDisk docs p5-14).\n",value);
			break;
	}
	return(-1);
}
*/

#ifdef SD_VERIFY
/*
-------------------------------------------------------------------------------
							verify
-------------------------------------------------------------------------------
*/
DRESULT SdCard :: verify(const uint8_t* buf, uint32_t address )
{
    static uint8_t rb_buffer[SD_SECTOR_SIZE];
    DRESULT read_resp;
    int i;
    
    read_resp = read(address, &rb_buffer[0], 1);
    if(read_resp) {
        return read_resp;
    }
    for(i=0;i<SD_SECTOR_SIZE;i++) {
        if(rb_buffer[i] != buf[i]) {
            printf("VERIFY ERROR!\n");
            return RES_ERROR;
        }
    }
    return RES_OK;
}
#endif

/*
-------------------------------------------------------------------------------
							get_drive_size
							==============
  Abstract:
	Calculates size of card from CSD 

  Parameters
	drive_size:	pointer to a 32bits variable receiving the size

  Return:
	0:		no error
	other:	error
-------------------------------------------------------------------------------
*/
#define SD_DRIVE_SIZE
#ifdef SD_DRIVE_SIZE

DRESULT SdCard :: get_drive_size(uint32_t* drive_size)
{
	uint8_t cardresp, i, by, q;
	uint8_t iob[16];
	uint16_t c_size;
    int sector_power;
	
/*
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("SdCard unavailable.\n");
    	return RES_NOTRDY;
    }
*/
    ENTER_SAFE_SECTION
    sdio_send_command(CMDREADCID, 0, 0);
	
    q = 200;
	do {
		cardresp = Resp8b();
		q--;
	} while ((cardresp != 0xFE) && (q));

    if(q) {
    	for( i=0; i<16; i++) {
    		iob[i] = SDIO_DATA;
    	}
    }
    LEAVE_SAFE_SECTION

	if (q) {
		printf("CID: ");
		for(i=0; i < 16; i++) {
			printf(" %02x", iob[i]);
		} printf("\n");
	} else {
        printf("Failed to read Card ID.\n");
    }


    ENTER_SAFE_SECTION
	sdio_send_command(CMDREADCSD, 0, 0);
	
    q = 200;
	do {
		cardresp = Resp8b();
		q--;
	} while ((cardresp != 0xFE) && (q));

    if(q) {
    	for( i=0; i<16; i++) {
    		iob[i] = SDIO_DATA;
    	}
    }
	SDIO_DATA = 0xff;
	SDIO_DATA = 0xff;

	LEAVE_SAFE_SECTION

    if(q) {
        printf("CSD:");
    	for( i=0; i<16; i++) {
    		printf(" %02x", iob[i]);
    	}
    	printf("\n");
    } else {
        printf("Failed to read CSD.\n");
        *drive_size = 0L;
        return RES_ERROR;
    }

    if((iob[0] & 0x03) == 0) { // type 1.0 CSD
    	c_size = iob[6] & 0x03; // bits 1..0
    	c_size <<= 10;
    	c_size += (uint16_t)iob[7]<<2;
    	c_size += iob[8]>>6;
    
    	int read_bl_power = (int)(iob[5] & 0x0F);
        
    	by=iob[9] & 0x03;
    	by <<= 1;
    	by += iob[10] >> 7;
    	
        int c_size_power  = (int)(2+by);
        sector_power  = (c_size_power + read_bl_power) - 9; // divide by 512 to get sector count
        printf("read_bl_power = %d. size_power = %d. c_size = %d.\n", read_bl_power, c_size_power, c_size);
    } else { // assume 2.0 CSD
        c_size = (uint32_t)iob[9];
        c_size += ((uint32_t)iob[8]) << 8;
        c_size += ((uint32_t)(iob[7] & 0x3F)) << 16;
        sector_power = 10; // 512K per c_size, by definition. 512 bytes per sector, thus 2^10 sectors per c_size count
        printf("CSD 2.0: c_size = %d.\n", c_size);
    }

	*drive_size = (uint32_t)(c_size+1) << sector_power;
	
/*	xSemaphoreGive(mutex);*/
	return RES_OK;
}
#endif
