extern "C" {
    #include "itu.h"
    #include "small_printf.h"
}
#include "at49_flash.h"
#include <string.h>

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
//	{ FLASH_ID_BOOTFPGA,   0x01, 0x000000, 0x000000, 0x29570 },
	{ FLASH_ID_CONFIG,     0x00, 0x002000, 0x002000, 0x0E000 },
	{ FLASH_ID_AR5PAL,     0x00, 0x010000, 0x010000, 0x08000 },
	{ FLASH_ID_AR6PAL,     0x00, 0x018000, 0x018000, 0x08000 },
	{ FLASH_ID_FINAL3,     0x00, 0x020000, 0x020000, 0x10000 },
	{ FLASH_ID_SOUNDS,     0x00, 0x030000, 0x030000, 0x05000 },
	{ FLASH_ID_CHARS,      0x00, 0x038000, 0x038000, 0x01000 },
	{ FLASH_ID_SIDCRT,     0x00, 0x038000, 0x038000, 0x02000 },
	{ FLASH_ID_EPYX,       0x00, 0x0B0000, 0x0B0000, 0x02000 },
	{ FLASH_ID_ROM1541,    0x00, 0x03C000, 0x03C000, 0x04000 },
	{ FLASH_ID_RR38PAL,    0x00, 0x040000, 0x040000, 0x10000 },
	{ FLASH_ID_SS5PAL,     0x00, 0x050000, 0x050000, 0x10000 },
	{ FLASH_ID_AR5NTSC,    0x00, 0x060000, 0x060000, 0x08000 },
	{ FLASH_ID_ROM1541C,   0x00, 0x068000, 0x068000, 0x04000 },
	{ FLASH_ID_ROM1541II,  0x00, 0x06C000, 0x06C000, 0x04000 },
	{ FLASH_ID_RR38NTSC,   0x00, 0x070000, 0x070000, 0x10000 },
	{ FLASH_ID_SS5NTSC,    0x00, 0x080000, 0x080000, 0x10000 },
	{ FLASH_ID_TAR_PAL,    0x00, 0x090000, 0x090000, 0x10000 },
	{ FLASH_ID_TAR_NTSC,   0x00, 0x0A0000, 0x0A0000, 0x10000 },
	{ FLASH_ID_LIST_END,   0x00, 0x1FE000, 0x1FE000, 0x01000 } };


AT49_Flash::AT49_Flash()
{
    page_size    = 8192;        // in bytes
	sector_count = 39;			// default to AT49DB161
    total_size   = 256;		    // in pages, default to AT49DB163
}
    
AT49_Flash::~AT49_Flash()
{
    debug(("AT49 Flash class destructed..\n"));
}

int AT49_Flash::sector_to_addr(int sector)
{
    if(sector < 0)
        return -1;
    if(sector >= sector_count)
        return -1;
        
/* BOTTOM BOOT */
    int page;
    if(sector < 8)
        page = sector;
    else
        page = (sector - 7) * 8;

    return page * page_size;
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
    BYTE *src = (BYTE *)(AT49_BASE + device_addr);
    printf("Memcpy %d bytes from %p to %p.\n", len, src, buffer);
    memcpy(buffer, src, len);
}
    
AT49_Flash *AT49_Flash :: tester()
{
    AT49_COMMAND1 = 0xAA;
    AT49_COMMAND2 = 0x55;
    AT49_COMMAND1 = 0x90;
	BYTE manuf  = AT49_MEM_ARRAY(0);
	BYTE dev_id = AT49_MEM_ARRAY(2);
    // exit ProductID
    AT49_COMMAND1 = 0xAA;
    AT49_COMMAND2 = 0x55;
    AT49_COMMAND1 = 0xF0;
    
    //printf("AT49 manufacturer / device ID: %b / %b\n", manuf, dev_id);
    if(manuf != 0x1F)
    	return NULL; // not Atmel

	if(dev_id != 0xC0)
	    return NULL; // not AT49DB163D

    // configure status register
    AT49_COMMAND1 = 0xAA;
    AT49_COMMAND2 = 0x55;
    AT49_COMMAND1 = 0xD0;
    AT49_COMMAND2 = 0x01;

	return this;
}

int AT49_Flash :: get_page_size(void)
{
    return page_size;
}

int AT49_Flash :: get_sector_size(int addr)
{
    int page = addr / page_size;
    int sector = page_to_sector(page);
/* BOTTOM BOOT */
    if(sector < 8)
        return page_size;

    return 8 * page_size;        
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
    read_dev_addr(addr, len, buffer);
}


bool AT49_Flash :: wait_ready(int time_out)
{
    ITU_TIMER = 200;
	bool ret = true;
    do {
		last_status = AT49_MEM_ARRAY(0);
		if(last_status & 0x20) {
			debug(("Flash protected.\n"));
			ret = false;
			break;
		}
		if(last_status & 0x80) {
			ret = true;
			break;
		}
        if(!ITU_TIMER) {
            if(!time_out) {
                debug(("Flash timeout.\n"));
                ret = false;
			    break;
			}
			time_out--;
			ITU_TIMER = 200;
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
    int device_addr = (AT49_PAGE_CONFIG_START + page) * page_size;
	read_dev_addr(device_addr, length, buffer);
}

void AT49_Flash :: write_config_page(int page, void *buffer)
{
    page += AT49_PAGE_CONFIG_START;
    int sector = page_to_sector(page);
    if(erase_sector(sector))
	    write_page(page, buffer);
	else
	    printf("Configuration page was not written, because erase was not successful.\n");
	    
}

bool AT49_Flash :: write_page(int page, void *buffer)
{
    // pre-condition: page is erased, status register mode = 01

    BYTE *src = (BYTE *)buffer;
    volatile BYTE *dest = &(AT49_MEM_ARRAY(page * page_size));

    printf("AT49: Write page %d. Addr = %p..", page, dest);

    bool success = true;
    for(int i=0;i<page_size;i++) {
        AT49_COMMAND1 = 0xAA;
        AT49_COMMAND2 = 0x55;
        AT49_COMMAND1 = 0xA0;
        *(dest++) = *(src++);
        if(!wait_ready(2)) {
            success = false;
            break;
        }
    }    
    // exit ProductID
    AT49_COMMAND1 = 0xAA;
    AT49_COMMAND2 = 0x55;
    AT49_COMMAND1 = 0xF0;

    printf(success?"success\n":" FAIL!\n");
    return success;
}

bool AT49_Flash :: erase_sector(int sector)
{
    int addr = sector_to_addr(sector);

    printf("AT49: Erase sector %d. Addr = %p\n", sector, addr);

	// erase block in flash
    AT49_COMMAND1 = 0xAA;
    AT49_COMMAND2 = 0x55;
    AT49_COMMAND1 = 0x80;

    AT49_COMMAND1 = 0xAA;
    AT49_COMMAND2 = 0x55;

    AT49_MEM_ARRAY(addr) = 0x30;

    return wait_ready(6000);
}

int AT49_Flash :: page_to_sector(int page)
{
/* BOTTOM BOOT */
    if(page < 8)
        return page;
    return (page >> 3) + 7;
}

void AT49_Flash :: reboot(int addr)
{
    // not yet possible, because our ZPU ram is preventing us from writing the right RAM
}

bool AT49_Flash :: protect_configure(void)
{
    return false;
}

void AT49_Flash :: protect_disable(void)
{
}

void AT49_Flash :: protect_enable(void)
{
}
