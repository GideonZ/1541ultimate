#include "at49_flash.h"
#include "small_printf.h"

#ifdef BOOTLOADER
#define debug(x)
#define error(x)
#else
#define debug(x)
#define error(x) printf x
#endif

AT49_Flash at49_flash;

//#define debug(x)

// FPGA = ~0x29570 in length
// MAX APPL length = 500K => 969 pages / 4 sectors
// CUSTOM FPGA is thus on sector 12.. 

static const t_flash_address flash_addresses[] = {
	{ FLASH_ID_BOOTAPP,    0x01, 0x02A000, 0x02A000, 0x0C000 }, // 192 pages (48K)
	{ FLASH_ID_APPL,       0x01, 0x100000, 0x100000, 0x80000 },
#ifndef BOOTLOADER                                   
	{ FLASH_ID_BOOTFPGA,   0x01, 0x000000, 0x000000, 0x29570 },
	{ FLASH_ID_AR5PAL,     0x00, 0x060000, 0x060000, 0x08000 },
	{ FLASH_ID_AR6PAL,     0x00, 0x068000, 0x068000, 0x08000 },
	{ FLASH_ID_FINAL3,     0x00, 0x070000, 0x070000, 0x10000 },
	{ FLASH_ID_SOUNDS,     0x00, 0x080000, 0x080000, 0x05000 },
	{ FLASH_ID_CHARS,      0x00, 0x087000, 0x087000, 0x01000 },
	{ FLASH_ID_SIDCRT,     0x00, 0x088000, 0x088000, 0x02000 },
	{ FLASH_ID_EPYX,       0x00, 0x08A000, 0x08A000, 0x02000 },
	{ FLASH_ID_ROM1541,    0x00, 0x08C000, 0x08C000, 0x04000 },
	{ FLASH_ID_RR38PAL,    0x00, 0x090000, 0x090000, 0x10000 },
	{ FLASH_ID_SS5PAL,     0x00, 0x0A0000, 0x0A0000, 0x10000 },
	{ FLASH_ID_AR5NTSC,    0x00, 0x0B0000, 0x0B0000, 0x08000 },
	{ FLASH_ID_ROM1541C,   0x00, 0x0B8000, 0x0B8000, 0x04000 },
	{ FLASH_ID_ROM1541II,  0x00, 0x0BC000, 0x0BC000, 0x04000 },
	{ FLASH_ID_RR38NTSC,   0x00, 0x0C0000, 0x0C0000, 0x10000 },
	{ FLASH_ID_SS5NTSC,    0x00, 0x0D0000, 0x0D0000, 0x10000 },
	{ FLASH_ID_TAR_PAL,    0x00, 0x0E0000, 0x0E0000, 0x10000 },
	{ FLASH_ID_TAR_NTSC,   0x00, 0x0F0000, 0x0F0000, 0x10000 },
	{ FLASH_ID_CUSTOMFPGA, 0x01, 0x180000, 0x180000, 0x29570 },
	{ FLASH_ID_CONFIG,     0x00, 0x1FF000, 0x1FF000, 0x01000 },
#endif                                               
	{ FLASH_ID_LIST_END,   0x00, 0x1FE000, 0x1FE000, 0x01000 } };


AT49_Flash::AT49_Flash()
{
    page_size    = 528;         // in bytes
    block_size   = 8;           // in pages
	sector_size  = 256;			// in pages, default to AT49DB161
	sector_count = 16;			// default to AT49DB161
    total_size   = 4096;		// in pages, default to AT49DB161
    page_shift   = 10;        	// offset in address reg

}
    
AT49_Flash::~AT49_Flash()
{
    debug(("AT49 Flash class destructed..\n"));
}


void AT49_Flash :: get_image_addresses(int id, t_flash_address *addr)
{
	t_flash_address *a = (t_flash_address *)flash_addresses;
	while(a->id != FLASH_ID_LIST_END) {
		if(int(a->id) == id) {
			*addr = *a; // copy
			return;
		}
		a++;
	}
	*addr = *a;
	return; // return start value at end of list
}

void AT49_Flash :: read_dev_addr(int device_addr, int len, void *buffer)
{
    BYTE *buf = (BYTE *)buffer;
    for(int i=0;i<len;i++) {
        *(buf++) = 0; // ###
    }
}
    
//#ifndef BOOTLOADER
AT49_Flash *AT49_Flash :: tester()
{
	BYTE manuf  = 0; // ###
	BYTE dev_id = 0; // ###
    
    if(manuf != 0x1F)
    	return NULL; // not Atmel

	if(dev_id == 0x26) { // AT49DB161
		sector_size  = 256;	
		sector_count = 16;	
	    total_size   = 4096;
		return this;
	}
	if(dev_id == 0x27) { // AT49DB321
		sector_size  = 128;	
		sector_count = 64;	
	    total_size   = 8192;
		return this;
	}

	return NULL;
}
//#endif

#ifndef BOOTLOADER

int AT49_Flash :: get_page_size(void)
{
    return page_size;
}

int AT49_Flash :: get_sector_size(int addr)
{
    return page_size * sector_size;
}

int AT49_Flash :: read_image(int id, void *buffer, int buf_size)
{
	t_flash_address *a = (t_flash_address *)flash_addresses;
	while(a->id != FLASH_ID_LIST_END) {
		if(int(a->id) == id) {
			int len = (a->max_length > buf_size) ? buf_size : a->max_length;
			read_dev_addr(a->device_addr, len, buffer);
			return len;
		}
		a++;
	}
	return 0;
}
    
void AT49_Flash :: read_linear_addr(int addr, int len, void *buffer)
{
    int page = addr / page_size;
    int offset = addr - (page * page_size);
    int device_addr = (page << page_shift) | offset;
    
    debug(("Requested addr: %6x. Page: %d. Offset = %d. ADDR: %6x\n", addr, page, offset, device_addr));
    debug(("%b %b %b\n", BYTE(device_addr >> 16), BYTE(device_addr >> 8), BYTE(device_addr)));
    read_dev_addr(device_addr, len, buffer);
}

/*
void AT49_Flash :: write(int addr, int len, void *buffer)
{
    int page = addr / page_size;
    int offset = addr - (page * page_size);
    int device_addr = (page << page_shift) | offset;
    BYTE *buf = (BYTE *)buffer;
    
    if(page < FIRST_WRITABLE_PAGE) {
        error(("Error: Trying to write to a protected page (%d).\n", page));
        return;
    }

    debug(("Requested addr: %6x. Page: %d. Offset = %d. ADDR: %6x\n", addr, page, offset, device_addr));
    debug(("%b %b %b\n", BYTE(device_addr >> 16), BYTE(device_addr >> 8), BYTE(device_addr)));
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = AT49_MainMemoryPageProgramThroughBuffer1;
    SPI_FLASH_DATA = BYTE(device_addr >> 16);
    SPI_FLASH_DATA = BYTE(device_addr >> 8);
    SPI_FLASH_DATA = BYTE(device_addr);
    for(int i=0;i<len;i++) {
        SPI_FLASH_DATA = *(buf++);
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    wait_ready(5000);
}
*/

bool AT49_Flash :: wait_ready(int time_out)
{
// TODO: FOR Microblaze or any faster CPU, we need to insert wait cycles here
    int i=time_out;
	bool ret = true;
//    SPI_FLASH_CTRL = SPI_FORCE_SS;
//    SPI_FLASH_DATA = AT49_StatusRegisterRead;
    do {
		last_status = 0; // ###
		if(last_status & 0x10) {
			debug(("Flash protected.\n"));
			ret = false;
			break;
		}
		if(last_status & 0x80) {
			ret = true;
			break;
		}
        if((--i)<0) {
            debug(("Flash timeout.\n"));
            ret = false;
			break;
        }
    } while(true);
    return ret;
}
    
void AT49_Flash :: read_serial(void *buffer)
{
}

int  AT49_Flash :: get_config_page_size(void)
{
	return get_page_size();
}
	
int  AT49_Flash :: get_number_of_config_pages(void)
{
	return AT49_NUM_CONFIG_PAGES;
}

void AT49_Flash :: read_config_page(int page, int length, void *buffer)
{
    int device_addr = ((page + AT49_PAGE_CONFIG_START) << page_shift);
	read_dev_addr(device_addr, length, buffer);
}

void AT49_Flash :: write_config_page(int page, void *buffer)
{
	write_page(page + AT49_PAGE_CONFIG_START, buffer);
}

/*
void AT49_Flash :: read_page(int page, void *buffer)
{
    int device_addr = (page << page_shift);
    int len = (page_size >> 2);
    DWORD *buf = (DWORD *)buffer;
    
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = AT49_ContinuousArrayRead_LowFrequency;
    SPI_FLASH_DATA = BYTE(device_addr >> 16);
    SPI_FLASH_DATA = BYTE(device_addr >> 8);
    SPI_FLASH_DATA = BYTE(device_addr);
    for(int i=0;i<len;i++) {
        *(buf++) = SPI_FLASH_DATA_32;
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
}
*/

bool AT49_Flash :: write_page(int page, void *buffer)
{
}

bool AT49_Flash :: erase_sector(int sector)
{
}

int AT49_Flash :: page_to_sector(int page)
{
	return (page / sector_size);
}

void AT49_Flash :: reboot(int addr)
{
}

bool AT49_Flash :: protect_configure(void)
{
    return false;
}

void AT49_Flash :: protect_disable(void)
{
}

#endif

void AT49_Flash :: protect_enable(void)
{
}
