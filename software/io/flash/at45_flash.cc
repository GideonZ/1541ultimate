#include "at45_flash.h"
#include "icap.h"
extern "C" {
    #include "itu.h"
    #include "small_printf.h"
    #include "dump_hex.h"
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

AT45_Flash at45_flash;

//#define debug(x)

// Free space 0x103000 - 108000 (0x5000)
// FPGA = ~0x54000 in length
// MAX APPL length = 500K => 969 pages / 4 sectors
// CUSTOM FPGA is thus on sector 12.. 

static const t_flash_address flash_addresses[] = {
	{ FLASH_ID_BOOTFPGA,   0x01, 0x000000, 0x000000, 0x53CA0 },
	{ FLASH_ID_BOOTAPP,    0x01, 0x053CA0, 0x0A2800, 0x0F360 }, // 118 pages
	{ FLASH_ID_APPL,       0x01, 0x108000, 0x200000, 0xC0000 }, // exactly half way: page 2048. (max 5D2 pages, rounded to 5E0 pages)
    { FLASH_ID_FLASHDRIVE, 0x00, 0x1C9E00, 0x378000, 0x40000 }, // 256KB, will move when all internal roms are removed
	{ FLASH_ID_CONFIG,     0x00, 0x20DF00, 0x3FC000, 0x02100 }, // page 0xFF0
	{ FLASH_ID_LIST_END,   0x00, 0x20DCF0, 0x3FBC00, 0x00210 } };


AT45_Flash::AT45_Flash()
{

    page_size    = 528;         // in bytes
    block_size   = 8;           // in pages
	sector_size  = 256;			// in pages, default to AT45DB161
	sector_count = 16;			// default to AT45DB161
    total_size   = 4096;		// in pages, default to AT45DB161
    page_shift   = 10;        	// offset in address reg
    config_start = total_size - AT45_NUM_CONFIG_PAGES;
}
    
AT45_Flash::~AT45_Flash()
{
    debug(("AT45 Flash class destructed..\n"));
}


void AT45_Flash :: get_image_addresses(int id, t_flash_address *addr)
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

void AT45_Flash :: read_dev_addr(int device_addr, int len, void *buffer)
{
	portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = AT45_ContinuousArrayRead_LowFrequency;
    SPI_FLASH_DATA = uint8_t(device_addr >> 16);
    SPI_FLASH_DATA = uint8_t(device_addr >> 8);
    SPI_FLASH_DATA = uint8_t(device_addr);

    uint8_t *buf = (uint8_t *)buffer;
    for(int i=0;i<len;i++) {
        *(buf++) = SPI_FLASH_DATA;
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
	portEXIT_CRITICAL();
}
    
AT45_Flash *AT45_Flash :: tester()
{
	portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    SPI_FLASH_DATA = 0xFF;
    SPI_FLASH_CTRL = SPI_FORCE_SS;

	SPI_FLASH_DATA = AT45_ManufacturerandDeviceIDRead;
	uint8_t manuf = SPI_FLASH_DATA;
	uint8_t dev_id = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
	portEXIT_CRITICAL();
    
    if(manuf != 0x1F)
    	return NULL; // not Atmel

	if(dev_id == 0x26) { // AT45DB161
		sector_size  = 256;	
		sector_count = 16;	
	    total_size   = 4096;
        config_start = total_size - AT45_NUM_CONFIG_PAGES;
		return this;
	}
	if(dev_id == 0x27) { // AT45DB321
		sector_size  = 128;	
		sector_count = 64;	
	    total_size   = 8192;
        config_start = total_size - AT45_NUM_CONFIG_PAGES;
		return this;
	}

	return NULL;
}

int AT45_Flash :: get_page_size(void)
{
    return page_size;
}

int AT45_Flash :: get_sector_size(int addr)
{
    return page_size * sector_size;
}

int AT45_Flash :: read_image(int id, void *buffer, int buf_size)
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
    
void AT45_Flash :: read_linear_addr(int addr, int len, void *buffer)
{
    int page = addr / page_size;
    int offset = addr - (page * page_size);
    int device_addr = (page << page_shift) | offset;
    
    debug(("Requested addr: %6x. Page: %d. Offset = %d. ADDR: %6x\n", addr, page, offset, device_addr));
    debug(("%b %b %b\n", uint8_t(device_addr >> 16), uint8_t(device_addr >> 8), uint8_t(device_addr)));
    read_dev_addr(device_addr, len, buffer);
}

/*
void AT45_Flash :: write(int addr, int len, void *buffer)
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
    SPI_FLASH_DATA = AT45_MainMemoryPageProgramThroughBuffer1;
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

bool AT45_Flash :: wait_ready(int time_out)
{
	bool ret = true;
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = AT45_StatusRegisterRead;
    uint16_t start = getMsTimer();
	uint16_t now;

    do {
		last_status = SPI_FLASH_DATA;
/* it turns out that bit 1 is always set when protection is enabled, no matter whether this particular page was written or not.
		if(last_status & 0x02) {
			error(("Flash protected.\n"));
			ret = false;
			break;
		}
*/
		if(last_status & 0x80) {
			ret = true;
			break;
		}
    	now = getMsTimer();
    	if ((now - start) > time_out) {
    		debug(("Flash timeout.\n"));
            ret = false;
            break;
        }
    } while(true);
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    return ret;
}
    
void AT45_Flash :: read_serial(void *buffer)
{
	portENTER_CRITICAL();
    uint8_t *buf = (uint8_t *)buffer;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = AT45_ReadSecurityRegister;
    SPI_FLASH_DATA = 0;
    SPI_FLASH_DATA = 0;
    SPI_FLASH_DATA = 0;
    for(int i=0;i<64;i++) {
        SPI_FLASH_DATA = 0xFF; // read dummy
    }
    for(int i=0;i<64;i++) {
        *(buf++) = SPI_FLASH_DATA;
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
	portEXIT_CRITICAL();
}

int  AT45_Flash :: get_config_page_size(void)
{
	return get_page_size();
}
	
int  AT45_Flash :: get_number_of_config_pages(void)
{
	return AT45_NUM_CONFIG_PAGES;
}

void AT45_Flash :: read_config_page(int page, int length, void *buffer)
{
    int device_addr = ((page + config_start) << page_shift);
	read_dev_addr(device_addr, length, buffer);
}

void AT45_Flash :: write_config_page(int page, void *buffer)
{
	write_page(page + config_start, buffer);
}

void AT45_Flash :: clear_config_page(int page)
{
    page += config_start;   
    int device_addr = (page << page_shift);
    
    debug(("Page erase. %b %b %b\n", uint8_t(device_addr >> 16), uint8_t(device_addr >> 8), uint8_t(device_addr)));

	portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = AT45_PageErase;
    SPI_FLASH_DATA = uint8_t(device_addr >> 16);
    SPI_FLASH_DATA = uint8_t(device_addr >> 8);
    SPI_FLASH_DATA = uint8_t(device_addr);
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    wait_ready(500); // datasheet: 35
	portEXIT_CRITICAL();
}

bool AT45_Flash :: read_page(int page, void *buffer)
{
    int device_addr = (page << page_shift);
    int len = (page_size >> 2);
    uint32_t *buf = (uint32_t *)buffer;
    
	portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = AT45_ContinuousArrayRead_LowFrequency;
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

bool AT45_Flash :: write_page(int page, const void *buffer)
{
    int device_addr = (page << page_shift);
    int len = (page_size >> 2);
    uint32_t *buf = (uint32_t *)buffer;
    
	portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = AT45_MainMemoryPageProgramThroughBuffer2;
    SPI_FLASH_DATA = uint8_t(device_addr >> 16);
    SPI_FLASH_DATA = uint8_t(device_addr >> 8);
    SPI_FLASH_DATA = uint8_t(device_addr);
    for(int i=0;i<len;i++) {
        SPI_FLASH_DATA_32 = *(buf++);
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    bool ret = wait_ready(500); // datasheet: 40, including erase
	if(!ret) {
		portEXIT_CRITICAL();
		return false;
	}
	SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = AT45_MainMemoryPagetoBuffer2Compare;
    SPI_FLASH_DATA = uint8_t(device_addr >> 16);
    SPI_FLASH_DATA = uint8_t(device_addr >> 8);
    SPI_FLASH_DATA = uint8_t(device_addr);
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    ret = wait_ready(500); // datasheet: 40, including erase
    portEXIT_CRITICAL();
	if(!ret) {
		return false;
	}
	if(last_status & 0x40) { // compare failed
	    error(("AT45 write page: verify failed.\n"));
		return false;
    }
	return true;
}

bool AT45_Flash :: erase_sector(int sector)
{
    int page = sector * sector_size;
    int device_addr = (page << page_shift);
    
    debug(("Sector erase. %b %b %b\n", uint8_t(device_addr >> 16), uint8_t(device_addr >> 8), uint8_t(device_addr)));

	portENTER_CRITICAL();
	SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = AT45_SectorErase;
    SPI_FLASH_DATA = uint8_t(device_addr >> 16);
    SPI_FLASH_DATA = uint8_t(device_addr >> 8);
    SPI_FLASH_DATA = uint8_t(device_addr);
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    bool result = wait_ready(15000); // datasheet: 5 seconds!
    portEXIT_CRITICAL();
    return result;
}

int AT45_Flash :: page_to_sector(int page)
{
	return (page / sector_size);
}
/*
static BYTE rotate(BYTE t)
{
    BYTE r = 0;
    for(int b=0;b<8;b++) {
        r <<= 1;
        r |= (t & 1);
        t >>= 1;
    }
    return r;
}
*/
void AT45_Flash :: reboot(int addr)
{
    int page = addr / page_size;
    int offset = addr - (page * page_size);
    int device_addr = (page << page_shift) | offset;

    uint8_t icap_sequence[20] = { 0xAA, 0x99, 0x32, 0x61,    0,    0, 0x32, 0x81,    0,    0,
                               0x32, 0xA1, 0x00, 0x4F, 0x30, 0xA1, 0x00, 0x0E, 0x20, 0x00 };

    icap_sequence[4] = device_addr >> 8;
    icap_sequence[5] = device_addr >> 0;
    icap_sequence[9] = device_addr >> 16;
    icap_sequence[8] = AT45_ContinuousArrayRead_LowFrequency;

    for(int i=0;i<20;i++) {
        printf("%b ", icap_sequence[i]);
    }
    ICAP_PULSE = 0;
    ICAP_PULSE = 0;
    for(int i=0;i<20;i++) {
        ICAP_WRITE = icap_sequence[i];
    }
    ICAP_PULSE = 0;
    ICAP_PULSE = 0;
    error(("You should never see this!\n"));
}

bool AT45_Flash :: protect_configure(void)
{
	portENTER_CRITICAL();
	// erase sector protection regsiter
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA_32 = 0x3D2A7FCF;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
	if(!wait_ready(100)) { // datasheet doesn't specify
	    portEXIT_CRITICAL();
		return false;
	}

	// program sector protection regsiter
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA_32 = 0x3D2A7FFC;

	// by default, protect 3/4 of the device and leave 1/4 unprotected
	int prot = 3 * (sector_count >> 2);
	for(int i=0;i<prot;i++) {
		SPI_FLASH_DATA = 0xFF;
	}
	int not_prot = sector_count - prot;
	for(int i=0;i<not_prot;i++) {
		SPI_FLASH_DATA = 0x00;
	}
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    portEXIT_CRITICAL();

	if(!wait_ready(100))
		return false;

	return true;
}

void AT45_Flash :: protect_show_status(void)
{
    uint8_t *buffer = new uint8_t[sector_count];

	portENTER_CRITICAL();

	SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA_32 = 0x32323232;
    for(int i=0;i<sector_count;i++) {
        buffer[i] = SPI_FLASH_DATA;
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    portEXIT_CRITICAL();

    dump_hex_relative(buffer, sector_count);

	portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA_32 = 0x35353535;
    for(int i=0;i<sector_count;i++) {
        buffer[i] = SPI_FLASH_DATA;
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    portEXIT_CRITICAL();

    dump_hex_relative(buffer, sector_count);

    delete[] buffer;
}
    

void AT45_Flash :: protect_disable(void)
{
	portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA_32 = 0x3D2A7F9A;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
	portEXIT_CRITICAL();
}

void AT45_Flash :: protect_enable(void)
{
	portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA_32 = 0x3D2A7FA9;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
	portEXIT_CRITICAL();
}
