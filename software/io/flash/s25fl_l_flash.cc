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
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = 0x66;
    SPI_FLASH_DATA = 0x99;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;

    wait_ms(1);
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = 0x66;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = 0x99;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;

    wait_ms(1);
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = 0xFF;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;

    wait_ms(1);

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
    // printf("S25FL L MANUF: %b MEM_TYPE: %b CAPACITY: %b\n", manuf, mem_type, capacity);
	
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
		return "Infineon S25FL064L";
	case 65536:
		return "Infineon S25FL128L";
	case 131072:
		return "Infineon S25FL256L";
	default:
		return "Infineon";
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

bool S25FLxxxL_Flash :: write_page(int page, const void *buffer)
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
    uint32_t buf_addr = (uint32_t)buffer;
    if (buf_addr & 3) { // not aligned
        len *= 4;
        uint8_t *buf8 = (uint8_t *)buffer;
        for (int i = 0; i < len; i++) {
            SPI_FLASH_DATA = *(buf8++);
        }
    } else {
        for (int i = 0; i < len; i++) {
            SPI_FLASH_DATA_32 = *(buf++);
        }
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
    portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = S25FLL_ReadAnyRegister;
    SPI_FLASH_DATA = 0;
    SPI_FLASH_DATA = 0;
    SPI_FLASH_DATA = 0;
    SPI_FLASH_DATA = 0;
    uint8_t status = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    portEXIT_CRITICAL();
    printf("Status register before unlocking: %b\n", status);

    portENTER_CRITICAL();
    SPI_FLASH_CTRL = 0;
    SPI_FLASH_DATA = S25FLL_WriteEnable;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = S25FLL_WriteRegisters;
    SPI_FLASH_DATA = 0x00;
    SPI_FLASH_DATA = 0x00;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high

    wait_ready(50); // datasheet: 5 ms

    SPI_FLASH_CTRL = 0;
    SPI_FLASH_DATA = S25FLL_WriteDisable;
    portEXIT_CRITICAL();

    portENTER_CRITICAL();
    SPI_FLASH_CTRL = 0;
    SPI_FLASH_DATA = 0x66;
    SPI_FLASH_DATA = 0x99;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    portEXIT_CRITICAL();
}

bool S25FLxxxL_Flash :: protect_configure(int kilobytes)
{
    portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = S25FLL_ReadAnyRegister;
    SPI_FLASH_DATA = 0;
    SPI_FLASH_DATA = 0;
    SPI_FLASH_DATA = 0;
    SPI_FLASH_DATA = 0;
    uint8_t status = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    portEXIT_CRITICAL();

    // S25FL128L: BP[2:0] = { 0K, 256K, 512K,   1M,   2M, 4M, 8M, 16M } -- 65536
    // S25FL064L: BP[2:0] = { 0K, 128K, 256K, 512K,   1M, 2M, 4M,  8M } -- 32768

    // protect the LOWER PART of the device:
    // SEC = 0
    // TB = 1
    // BP[2:0] = xxx
    // SRP0, SEC, TB, BP2, BP1, BP0, WEL, BUSY
    //  0     0    x   x    x    0    0     0

    int bp = 0;
    switch(total_size) {
        case 32768: bp = kilobytes / 128; break;
        case 65536: bp = kilobytes / 256; break;
        default: bp = kilobytes / 64; break;
    }
    uint8_t bp_bits = 0x20 | (bp << 2);

    printf("Status register before locking: %b, requested: %b\n", status, bp_bits);
    if ((status & 0x7C) == bp_bits) { // already set
        return true;
    }

    portENTER_CRITICAL();
    SPI_FLASH_CTRL = 0;
    SPI_FLASH_DATA = S25FLL_WriteEnable;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = S25FLL_WriteRegisters;
    SPI_FLASH_DATA = bp_bits;
    SPI_FLASH_DATA = 0x00;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high

    wait_ready(50);

    SPI_FLASH_CTRL = 0;
    SPI_FLASH_DATA = S25FLL_WriteDisable;
    portEXIT_CRITICAL();
    return true;
}
