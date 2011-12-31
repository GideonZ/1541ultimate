#include "w25q_flash.h"
#include "icap.h"
extern "C" {
    #include "small_printf.h"
}

#ifdef BOOTLOADER
#define debug(x)
#define error(x)
#else
#define debug(x)
#define error(x) printf x
#endif

W25Q_Flash w25q_flash;

//#define debug(x)

// Free space 0x103000 - 108000 (0x5000)
// FPGA = ~0x54000 in length
// MAX APPL length = 500K => 969 pages / 4 sectors
// CUSTOM FPGA is thus on sector 12.. 

static const t_flash_address flash_addresses[] = {
	{ FLASH_ID_BOOTAPP,    0x01, 0x054000, 0x054000, 0x0C000 }, // 192 pages (48K)
	{ FLASH_ID_APPL,       0x01, 0x100000, 0x100000, 0x80000 },
	{ FLASH_ID_BOOTFPGA,   0x01, 0x000000, 0x000000, 0x53CA0 },
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
	{ FLASH_ID_CUSTOMFPGA, 0x01, 0x180000, 0x180000, 0x54000 },
	{ FLASH_ID_CONFIG,     0x00, 0x1FF000, 0x1FF000, 0x01000 },
	{ FLASH_ID_LIST_END,   0x00, 0x1FE000, 0x1FE000, 0x01000 } };


W25Q_Flash::W25Q_Flash()
{

	sector_size  = 16;			// in pages, default to W25Q16BV
	sector_count = 512;			// default to W25Q16BV
    total_size   = 8192;		// in pages, default to W25Q16BV

    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    SPI_FLASH_DATA = 0xFF;
}
    
W25Q_Flash::~W25Q_Flash()
{
    debug(("W25Q Flash class destructed..\n"));
}


void W25Q_Flash :: get_image_addresses(int id, t_flash_address *addr)
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

void W25Q_Flash :: read_dev_addr(int device_addr, int len, void *buffer)
{
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = W25Q_ContinuousArrayRead_LowFrequency;
    SPI_FLASH_DATA = BYTE(device_addr >> 16);
    SPI_FLASH_DATA = BYTE(device_addr >> 8);
    SPI_FLASH_DATA = BYTE(device_addr);

    BYTE *buf = (BYTE *)buffer;
    for(int i=0;i<len;i++) {
        *(buf++) = SPI_FLASH_DATA;
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
}
    
Flash *W25Q_Flash :: tester()
{
    SPI_FLASH_CTRL = SPI_FORCE_SS;

	SPI_FLASH_DATA = W25Q_JEDEC_ID;
	BYTE manuf = SPI_FLASH_DATA;
	BYTE mem_type = SPI_FLASH_DATA;
	BYTE capacity = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    
//    printf("W25Q MANUF: %b MEM_TYPE: %b CAPACITY: %b\n", manuf, mem_type, capacity);

    if(manuf != 0xEF)
    	return NULL; // not Winbond

	if(mem_type != 0x40) { // not the right type of flash
		return NULL;
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

	return NULL;
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
    
void W25Q_Flash :: read_linear_addr(int addr, int len, void *buffer)
{
    read_dev_addr(addr, len, buffer); // linear and device address are the same
}

void W25Q_Flash :: read_serial(void *buffer)
{
    BYTE *buf = (BYTE *)buffer;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = W25Q_ReadUniqueIDNumber;
    SPI_FLASH_DATA_32 = 0;
    for(int i=0;i<8;i++) {
        *(buf++) = SPI_FLASH_DATA;
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    //return 8;
}

int  W25Q_Flash :: get_config_page_size(void)
{
	return get_page_size();
}
	
int  W25Q_Flash :: get_number_of_config_pages(void)
{
	return W25Q_NUM_CONFIG_PAGES;
}

void W25Q_Flash :: read_config_page(int page, int length, void *buffer)
{
    page *= sector_size;
    page += W25Q_PAGE_CONFIG_START;
	int addr = page << W25Q_PageShift;
	read_dev_addr(addr, length, buffer);
}

void W25Q_Flash :: write_config_page(int page, void *buffer)
{
    page *= sector_size;
    page += W25Q_PAGE_CONFIG_START;
    erase_sector(page / sector_size); // silly.. divide and later multiply.. oh well!
	write_page(page, buffer);
}

void W25Q_Flash :: clear_config_page(int page)
{
    page *= sector_size;
    page += W25Q_PAGE_CONFIG_START;
    erase_sector(page / sector_size); // silly.. divide and later multiply.. oh well!
}
    
bool W25Q_Flash :: read_page(int page, void *buffer)
{
    int device_addr = (page << W25Q_PageShift);
    int len = 1 << (W25Q_PageShift - 2);
    DWORD *buf = (DWORD *)buffer;
    
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = W25Q_ContinuousArrayRead_LowFrequency;
    SPI_FLASH_DATA = BYTE(device_addr >> 16);
    SPI_FLASH_DATA = BYTE(device_addr >> 8);
    SPI_FLASH_DATA = BYTE(device_addr);
    for(int i=0;i<len;i++) {
        *(buf++) = SPI_FLASH_DATA_32;
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    return true;
}

bool W25Q_Flash :: write_page(int page, void *buffer)
{
    int device_addr = (page << W25Q_PageShift);
    int len = 1 << (W25Q_PageShift - 2);
    DWORD *buf = (DWORD *)buffer;
    
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteEnable;

    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = W25Q_PageProgram;
    SPI_FLASH_DATA = BYTE(device_addr >> 16);
    SPI_FLASH_DATA = BYTE(device_addr >> 8);
    SPI_FLASH_DATA = BYTE(device_addr);
    for(int i=0;i<len;i++) {
        SPI_FLASH_DATA_32 = *(buf++);
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    bool ret = wait_ready(5000);
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteDisable;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
	return ret;
}

bool W25Q_Flash :: erase_sector(int sector)
{
	int addr = (sector * sector_size) << W25Q_PageShift;

	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteEnable;
    debug(("Sector erase. %b %b %b\n", BYTE(addr >> 16), BYTE(addr >> 8), BYTE(addr)));
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    SPI_FLASH_DATA = W25Q_SectorErase;
    SPI_FLASH_DATA = BYTE(addr >> 16);
    SPI_FLASH_DATA = BYTE(addr >> 8);
    SPI_FLASH_DATA = BYTE(addr);
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    bool ret = wait_ready(50000);
    SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteDisable;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
	return ret;
}

int W25Q_Flash :: page_to_sector(int page)
{
	return (page / sector_size);
}
	
void W25Q_Flash :: reboot(int addr)
{
    BYTE icap_sequence[20] = { 0xAA, 0x99, 0x32, 0x61,    0,    0, 0x32, 0x81,    0,    0,
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
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteEnable;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA = W25Q_WriteStatusRegister;
	SPI_FLASH_DATA = 0x20;
	SPI_FLASH_DATA = 0x00;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high

	wait_ready(10000);

	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteDisable;
}

bool W25Q_Flash :: protect_configure(void)
{
	// protect the LOWER HALF of the device:
	// SEC = 0
	// TB = 1
	// BP[2:0] = 101
	// SRP0, SEC, TB, BP2, BP1, BP0, WEL, BUSY
	//  0     0    1   1    0    1    0     0
	
	// program status register with value 0x34
	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteEnable;
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA = W25Q_WriteStatusRegister;
	SPI_FLASH_DATA = 0x34;
	SPI_FLASH_DATA = 0x00;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high

	wait_ready(10000);

	SPI_FLASH_CTRL = 0;
	SPI_FLASH_DATA = W25Q_WriteDisable;
}

void W25Q_Flash :: protect_enable(void)
{
    SPI_FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
	SPI_FLASH_DATA = W25Q_ReadStatusRegister1;
	BYTE status = SPI_FLASH_DATA;
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS; // drive CSn high
    if ((status & 0x7C) != 0x34)
    	protect_configure();
}

bool W25Q_Flash :: wait_ready(int time_out)
{
// TODO: FOR Microblaze or any faster CPU, we need to insert wait cycles here
    int i=time_out;
	bool ret = true;
    SPI_FLASH_CTRL = SPI_FORCE_SS;
    SPI_FLASH_DATA = W25Q_ReadStatusRegister1;
    while(SPI_FLASH_DATA & 0x01) {
        if(!(i--)) {
            debug(("Flash timeout.\n"));
            ret = false;
        }
    }
    SPI_FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    return ret;
}
