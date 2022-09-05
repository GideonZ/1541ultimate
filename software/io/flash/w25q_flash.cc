#include "w25q_flash.h"
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

W25Q_Flash w25q_flash;

//#define debug(x)

static const t_flash_address flash_addresses[] = {
	{ FLASH_ID_BOOTFPGA,   0x01, 0x000000, 0x000000, 0x53CA0 }, // 1341 pages (rounded to 1344 pages)
	{ FLASH_ID_BOOTAPP,    0x01, 0x054000, 0x054000, 0x0E000 }, //  224 pages (56K)
	{ FLASH_ID_APPL,       0x01, 0x062000, 0x062000, 0xC6000 }, // 3168 pages (max size: 792K)
	{ FLASH_ID_FLASHDRIVE, 0x00, 0x128000, 0x128000, 0xC8000 }, // 3200 pages (max size: 800K)
	{ FLASH_ID_CONFIG,     0x00, 0x1F0000, 0x1F0000, 0x10000 }, //  256 pages
	{ FLASH_ID_LIST_END,   0x00, 0x1FE000, 0x1FE000, 0x01000 } };

static const t_flash_address flash_addresses_u2p[] = {
	{ FLASH_ID_BOOTFPGA,   0x00, 0x000000, 0x000000, 0x0C0000 },
	{ FLASH_ID_APPL,       0x00, 0x0C0000, 0x0C0000, 0x140000 }, // Max 1.25 MB
	{ FLASH_ID_FLASHDRIVE, 0x00, 0x200000, 0x200000, 0x1F0000 },  // free space: 1984 KB
	{ FLASH_ID_CONFIG,     0x00, 0x3F0000, 0x3F0000, 0x010000 },
	{ FLASH_ID_LIST_END,   0x00, 0x3FE000, 0x3FE000, 0x001000 } };

static const t_flash_address flash_addresses_u2pl[] = {
	{ FLASH_ID_BOOTFPGA,   0x00, 0x000000, 0x000000, 0x0C0000 },
	{ FLASH_ID_APPL,       0x00, 0x0A0000, 0x0A0000, 0x160000 }, // Max 1.375 MB
	{ FLASH_ID_FLASHDRIVE, 0x00, 0x200000, 0x200000, 0x5F0000 }, // free space: 6080 KB
	{ FLASH_ID_CONFIG,     0x00, 0x7F0000, 0x7F0000, 0x010000 },
	{ FLASH_ID_LIST_END,   0x00, 0x7FE000, 0x7FE000, 0x001000 } };

static const t_flash_address flash_addresses_u64[] = {
	{ FLASH_ID_BOOTFPGA,   0x00, 0x000000, 0x000000, 0x290000 }, // 282BD4
	{ FLASH_ID_APPL,       0x00, 0x290000, 0x290000, 0x170000 }, // Max 1.5 MB
	{ FLASH_ID_FLASHDRIVE, 0x00, 0x400000, 0x400000, 0x3E8000 }, // ends at 0x7E8000  (free space: 4000 KB)
	{ FLASH_ID_CONFIG,     0x00, 0x7E8000, 0x7E8000, 0x018000 },
	{ FLASH_ID_LIST_END,   0x00, 0x7FE000, 0x7FE000, 0x001000 } };

W25Q_Flash::W25Q_Flash()
{
	sector_size  = 16;			// in pages, default to W25Q16BV
	sector_count = 512;			// default to W25Q16BV
    total_size   = 8192;		// in pages, default to W25Q16BV
}
    
W25Q_Flash::~W25Q_Flash()
{
    debug(("W25Q Flash class destructed..\n"));
}


void W25Q_Flash :: get_image_addresses(int id, t_flash_address *addr)
{
	t_flash_address *a;
	if (getFpgaCapabilities() & CAPAB_ULTIMATE64) {
		a = (t_flash_address *)flash_addresses_u64;
	} else if (getFpgaCapabilities() & CAPAB_ULTIMATE2PLUS) {
		if (getFpgaCapabilities() & CAPAB_FPGA_TYPE) {
			a = (t_flash_address *)flash_addresses_u2pl;
		} else {
			a = (t_flash_address *)flash_addresses_u2p;
		}
	} else {
		a = (t_flash_address *)flash_addresses;
	}
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

void W25Q_Flash :: read_dev_addr(int device_addr, int len, void *buffer)
{
	portENTER_CRITICAL();
	SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = W25Q_ContinuousArrayRead_LowFrequency;
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
    
Flash *W25Q_Flash :: tester()
{
	portENTER_CRITICAL();
	SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    SPI_FLASH_DATA = 0xFF;
    SPI_FLASH_CTRL = SPI_FORCE_SS;

	SPI_FLASH_DATA = W25Q_JEDEC_ID;
	uint8_t manuf = SPI_FLASH_DATA;
	uint8_t mem_type = SPI_FLASH_DATA;
	uint8_t capacity = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    portEXIT_CRITICAL();
    
    if (manuf != 0xEF) 
    	return NULL; // not Winbond

	if(mem_type != 0x40) { // not the right type of flash
		return NULL;
	}

    //printf("W25Q MANUF: %02x MEM_TYPE: %02x CAPACITY: %02x\n", manuf, mem_type, capacity);

    if(capacity == 0x14) { // 8 Mbit
		sector_size  = 16;
		sector_count = 256;
	    total_size   = 4096;
		return this;
	}
	if(capacity == 0x15) { // 16 Mbit
		sector_size  = 16;	
		sector_count = 512;	
	    total_size   = 8192;
		return this;
	}
	if(capacity == 0x16) { // 32 Mbit
		sector_size  = 16;	
		sector_count = 1024;	
	    total_size   = 16384;
		return this;
	}
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

	return NULL;
}

const char *W25Q_Flash :: get_type_string(void)
{
	switch(total_size) {
	case 4096:
		return "W25Q80";
	case 8192:
		return "W25Q16";
	case 16384:
		return "W25Q32";
	case 32768:
		return "W25Q64";
	case 65536:
		return "W25Q128";
	default:
		return "Winbond";
	}
}

int W25Q_Flash :: get_page_size(void)
{
    return (1 << W25Q_PageShift);
}

int W25Q_Flash :: get_sector_size(int addr)
{
    return get_page_size() * sector_size;
}

int W25Q_Flash :: read_image(int id, void *buffer, int buf_size)
{
	t_flash_address *a;
	if (getFpgaCapabilities() & CAPAB_ULTIMATE64) {
		a = (t_flash_address *)flash_addresses_u64;
	} else if (getFpgaCapabilities() & CAPAB_ULTIMATE2PLUS) {
		a = (t_flash_address *)flash_addresses_u2p;
	} else {
		a = (t_flash_address *)flash_addresses;
	}
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
    
void W25Q_Flash :: read_linear_addr(int addr, int len, void *buffer)
{
    read_dev_addr(addr, len, buffer); // linear and device address are the same
}

void W25Q_Flash :: read_serial(void *buffer)
{
    uint8_t *buf = (uint8_t *)buffer;
    portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = W25Q_ReadUniqueIDNumber;
    SPI_FLASH_DATA_32 = 0;
    for(int i=0;i<8;i++) {
        *(buf++) = SPI_FLASH_DATA;
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    portEXIT_CRITICAL();
    //return 8;
}

int  W25Q_Flash :: get_config_page_size(void)
{
	return get_page_size() * 2; // allow a bit bigger pages
}
	
int  W25Q_Flash :: get_number_of_config_pages(void)
{
	return W25Q_NUM_CONFIG_PAGES;
}

void W25Q_Flash :: read_config_page(int page, int length, void *buffer)
{
    page += sector_count - get_number_of_config_pages();
    page *= sector_size;
	int addr = page << W25Q_PageShift;
	read_dev_addr(addr, length, buffer);
}

void W25Q_Flash :: write_config_page(int page, void *buffer)
{
    page += sector_count - get_number_of_config_pages();
    page *= sector_size;
    erase_sector(page / sector_size); // silly.. divide and later multiply.. oh well!
    uint8_t *buf = (uint8_t *)buffer;
    write_page(page, buf);
	write_page(page+1, buf + get_page_size());
}

void W25Q_Flash :: clear_config_page(int page)
{
    page += sector_count - get_number_of_config_pages();
    page *= sector_size;
    erase_sector(page / sector_size); // silly.. divide and later multiply.. oh well!
}
    
bool W25Q_Flash :: read_page(int page, void *buffer)
{
    int device_addr = (page << W25Q_PageShift);
    int len = 1 << (W25Q_PageShift - 2);
    uint32_t *buf = (uint32_t *)buffer;
    
    portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = W25Q_ContinuousArrayRead_LowFrequency;
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

bool W25Q_Flash :: write_page(int page, const void *buffer)
{
    int device_addr = (page << W25Q_PageShift);
    int len = 1 << (W25Q_PageShift - 2);
    uint32_t *buf = (uint32_t *)buffer;
    
    portENTER_CRITICAL();
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteEnable;

    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = W25Q_PageProgram;
    SPI_FLASH_DATA = uint8_t(device_addr >> 16);
    SPI_FLASH_DATA = uint8_t(device_addr >> 8);
    SPI_FLASH_DATA = uint8_t(device_addr);
    for(int i=0;i<len;i++) {
        SPI_FLASH_DATA_32 = *(buf++);
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    bool ret = wait_ready(15); // datasheet: max 3 ms for page write, spansion: 5 ms max
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteDisable;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    portEXIT_CRITICAL();
	return ret;
}

bool W25Q_Flash :: erase_sector(int sector)
{
	int addr = (sector * sector_size) << W25Q_PageShift;

    portENTER_CRITICAL();
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteEnable;
    debug(("Sector erase. %b %b %b\n", uint8_t(addr >> 16), uint8_t(addr >> 8), uint8_t(addr)));
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = W25Q_SectorErase;
    SPI_FLASH_DATA = uint8_t(addr >> 16);
    SPI_FLASH_DATA = uint8_t(addr >> 8);
    SPI_FLASH_DATA = uint8_t(addr);
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    bool ret = wait_ready(1000); // datasheet: max 400 ms, spansion: 200 ms
    SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteDisable;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    portEXIT_CRITICAL();
	return ret;
}

int W25Q_Flash :: page_to_sector(int page)
{
	return (page / sector_size);
}
	
void W25Q_Flash :: reboot(int addr)
{
    uint8_t icap_sequence[20] = { 0xAA, 0x99, 0x32, 0x61,    0,    0, 0x32, 0x81,    0,    0,
                               0x32, 0xA1, 0x00, 0x4F, 0x30, 0xA1, 0x00, 0x0E, 0x20, 0x00 };

    icap_sequence[4] = (addr >> 8);
    icap_sequence[5] = (addr >> 0);
    icap_sequence[9] = (addr >> 16);
    icap_sequence[8] = W25Q_ContinuousArrayRead_LowFrequency;

    ICAP_PULSE = 0;
    ICAP_PULSE = 0;
    for(int i=0;i<20;i++) {
        ICAP_WRITE = icap_sequence[i];
    }
    ICAP_PULSE = 0;
    ICAP_PULSE = 0;
    debug(("You should never see this!\n"));
}

void W25Q_Flash :: protect_disable(void)
{
	// unprotect the device
	// SEC = 0
	// TB = 1
	// BP[2:0] = 000
	// SRP0, SEC, TB, BP2, BP1, BP0, WEL, BUSY
	//  0     0    1   0    0    0    0     0
	
	// program status register with value 0x20
    portENTER_CRITICAL();

    SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteEnable;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA = W25Q_WriteStatusRegister1;
	SPI_FLASH_DATA = 0x00;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high

	wait_ready(50); // datasheet: max 15 ms

    SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteEnable;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA = W25Q_WriteStatusRegister2;
	SPI_FLASH_DATA = 0x00;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high

	wait_ready(50); // datasheet: max 15 ms

	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteDisable;
    portEXIT_CRITICAL();
}

bool W25Q_Flash :: protect_configure(void)
{
	// to protect the LOWER 7/8 of the device,
	// the the following bits need to be set:
	// WPS = 0, CMP = 1
	// SEC TB BP2 BP1 BP0 = 0 0 1 0 0
	// CMP is bit 6 in Status Register 2. (0x40)

	// protect the LOWER QUARTER of the device:
	// SEC = 0
	// TB = 1
	// BP[2:0] = 101
	// SRP0, SEC, TB, BP2, BP1, BP0, WEL, BUSY
	//  0     0    1   1    0    1    0     0
	
	// protect the LOWER HALF of the device:
	// SEC = 0
	// TB = 1
	// BP[2:0] = 101
	// SRP0, SEC, TB, BP2, BP1, BP0, WEL, BUSY
	//  0     0    1   1    1    0    0     0

	// program status register with value 0x34 for 8MB and 0x38 for 4MB devices
    portENTER_CRITICAL();
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteEnable;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA = W25Q_WriteStatusRegister1;
	SPI_FLASH_DATA = (total_size >= 32768) ? 0x34 : 0x38;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
	wait_ready(50);

	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteEnable;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA = W25Q_WriteStatusRegister2;
	SPI_FLASH_DATA = 0x00;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
	wait_ready(50);

	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteDisable;
    portEXIT_CRITICAL();
	return true;
}

void W25Q_Flash :: protect_enable(void)
{
    portENTER_CRITICAL();
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA = W25Q_ReadStatusRegister1;
	uint8_t status = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    portEXIT_CRITICAL();

    if ((status & 0x7C) != 0x10)
    	protect_configure();
}

bool W25Q_Flash :: wait_ready(int time_out)
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
