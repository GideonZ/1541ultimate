#include "s25fl_l_flash.h"
#include "icap.h"
extern "C" {
    #include "small_printf.h"
}

#ifndef OS
#define debug(x)
#define error(x)
#define portENTER_CRITICAL()
#define portEXIT_CRITICAL()
#else
#include "FreeRTOS.h"
#define debug(x)
#define error(x) printf x
#endif

S25FLxxxL_Flash s25flxxxl_flash;

S25FLxxxL_Flash::S25FLxxxL_Flash()
{
	sector_size  = 16;			// in pages, default to S25FL16BV
	sector_count = 512;			// default to S25FL16BV
    total_size   = 8192;		// in pages, default to S25FL16BV
}
    
S25FLxxxL_Flash::~S25FLxxxL_Flash()
{
    debug(("S25FL 'L' Flash class destructed..\n"));
}
    
Flash *S25FLxxxL_Flash :: tester()
{
	portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    SPI_FLASH_DATA = 0xFF;

    SPI_FLASH_CTRL = SPI_FORCE_SS;

	SPI_FLASH_DATA = S25FLL_JEDEC_ID;
	uint8_t manuf = SPI_FLASH_DATA;
	uint8_t mem_type = SPI_FLASH_DATA;
	uint8_t capacity = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    
	portEXIT_CRITICAL();


    if (manuf != 0x01) 
    	return NULL; // not Cypress (was Spansion)

	if (mem_type != 0x60) { // not the right type of flash
		return NULL;
	}
    printf("S25FL L MANUF: %b MEM_TYPE: %b CAPACITY: %b\n", manuf, mem_type, capacity);
	
	if(capacity == 0x17) { // 64 Mbit
		sector_size  = 16;
		sector_count = 2048;
	    total_size   = 32768;
		return this;
	}
	if(capacity == 0x18) { // 128 Mbit
		sector_size  = 16;	
		sector_count = 4096;
	    total_size   = 65536;
		return this;
	}
/*
	if(capacity == 0x19) { // 256 Mbit
		sector_size  = 16;	
		sector_count = 8192;
	    total_size   = 131072;
		return this;
	}
*/

	return NULL;
}

const char *S25FLxxxL_Flash :: get_type_string(void)
{
	switch(total_size) {
	case 32768:
		return "S25FL064L";
	case 65536:
		return "S25FL128L";
	case 131072:
		return "S25FL256L";
	default:
		return "Cypress";
	}
}

bool S25FLxxxL_Flash :: read_page(int page, void *buffer)
{
    int device_addr = (page << S25FLL_PageShift);
    int len = 1 << (S25FLL_PageShift - 2);
    uint32_t *buf = (uint32_t *)buffer;

    portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = S25FLL_Read32;
    SPI_FLASH_DATA = uint8_t(device_addr >> 24);
    SPI_FLASH_DATA = uint8_t(device_addr >> 16);
    SPI_FLASH_DATA = uint8_t(device_addr >> 8);
    SPI_FLASH_DATA = uint8_t(device_addr);
    for(int i=0;i<len;i++) {
        *(buf++) = SPI_FLASH_DATA_32;
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    portEXIT_CRITICAL();

    return true;
}

bool S25FLxxxL_Flash :: write_page(int page, void *buffer)
{
    int device_addr = (page << S25FLL_PageShift);
    int len = 1 << (S25FLL_PageShift - 2);
    uint32_t *buf = (uint32_t *)buffer;

    portENTER_CRITICAL();
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = S25FLL_WriteEnable;

    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = S25FLL_PageProgram32;
    SPI_FLASH_DATA = uint8_t(device_addr >> 24);
    SPI_FLASH_DATA = uint8_t(device_addr >> 16);
    SPI_FLASH_DATA = uint8_t(device_addr >> 8);
    SPI_FLASH_DATA = uint8_t(device_addr);
    for(int i=0;i<len;i++) {
        SPI_FLASH_DATA_32 = *(buf++);
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    bool ret = wait_ready(15); // datasheet: max 3 ms for page write, spansion: 5 ms max
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = S25FLL_WriteDisable;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    portEXIT_CRITICAL();
	return ret;
}

bool S25FLxxxL_Flash :: erase_sector(int sector)
{
	int addr = (sector * sector_size) << S25FLL_PageShift;

    portENTER_CRITICAL();
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = S25FLL_WriteEnable;
    debug(("Sector erase. %b %b %b %b\n", uint8_t(addr >> 24), uint8_t(addr >> 16), uint8_t(addr >> 8), uint8_t(addr)));
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = S25FLL_SectorErase32;
    SPI_FLASH_DATA = uint8_t(addr >> 24);
    SPI_FLASH_DATA = uint8_t(addr >> 16);
    SPI_FLASH_DATA = uint8_t(addr >> 8);
    SPI_FLASH_DATA = uint8_t(addr);
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    bool ret = wait_ready(1000); // datasheet: max 400 ms, spansion: 200 ms
    SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = S25FLL_WriteDisable;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    portEXIT_CRITICAL();
	return ret;
}

void S25FLxxxL_Flash :: protect_disable(void)
{
/*
	portENTER_CRITICAL();
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = S25FLL_WriteEnable;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA = S25FLL_WriteRegisters;
	SPI_FLASH_DATA = 0x20;
	SPI_FLASH_DATA = 0x00;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high

	wait_ready(50); // datasheet: 5 ms

	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = S25FLL_WriteDisable;
	portEXIT_CRITICAL();
*/
}

bool S25FLxxxL_Flash :: protect_configure(void)
{
/*
	portENTER_CRITICAL();
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = S25FLL_WriteEnable;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA = S25FLL_WriteRegisters;
	SPI_FLASH_DATA = 0x34;
	SPI_FLASH_DATA = 0x00;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high

	wait_ready(50);

	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = S25FLL_WriteDisable;
	portEXIT_CRITICAL();
*/
	return true;
}

void S25FLxxxL_Flash :: protect_enable(void)
{
/*
	portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA = S25FLL_ReadStatusRegister1;
	uint8_t status = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
	portEXIT_CRITICAL();

    if ((status & 0x7C) != 0x34)
    	protect_configure();
*/
}

// quick fix
static bool wait_ready_s(int time_out)
{
	bool ret = true;
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = W25Q_ReadStatusRegister1;
    uint16_t start = getMsTimer();
	uint16_t now;
    while(SPI_FLASH_DATA & 0x01) {
    	now = getMsTimer();
    	if ((now - start) > time_out) {
    		debug(("Flash timeout.\n"));
            ret = false;
            break;
        }
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    return ret;
}

uint8_t S25FLxxxL_Flash :: write_config_register(uint8_t *was)
{
	portENTER_CRITICAL();

	SPI_FLASH_CTRL = SPI_FORCE_SS;
	SPI_FLASH_DATA = S25FLL_ReadAnyRegister;
	SPI_FLASH_DATA = 0x00;
	SPI_FLASH_DATA = 0x00;
	SPI_FLASH_DATA = 0x04;
	*was = SPI_FLASH_DATA;
	*was = SPI_FLASH_DATA;
	*was = SPI_FLASH_DATA;
	*was = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high

	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = S25FLL_WriteEnable;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA = S25FLL_WriteAnyRegister;

	// address 000004 = CR3NV
	SPI_FLASH_DATA = 0x00;
	SPI_FLASH_DATA = 0x00;
	SPI_FLASH_DATA = 0x04;

	// Data to write: default = 0x78, setting to 0x70 to remove dummy cycles
	SPI_FLASH_DATA = 0x70;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high

	wait_ready_s(50); // datasheet: 5 ms

	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = S25FLL_WriteDisable;

	SPI_FLASH_CTRL = SPI_FORCE_SS;
	SPI_FLASH_DATA = S25FLL_ReadAnyRegister;
	SPI_FLASH_DATA = 0x00;
	SPI_FLASH_DATA = 0x00;
	SPI_FLASH_DATA = 0x04;
	uint8_t read = SPI_FLASH_DATA;
	read = SPI_FLASH_DATA;
	read = SPI_FLASH_DATA;
	read = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high

	portEXIT_CRITICAL();
	return read;
}
