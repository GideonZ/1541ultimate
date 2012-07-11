#ifndef FLASH_H
#define FLASH_H

#include "integer.h"

#define FLASH_ID_BOOTFPGA   0x00
#define FLASH_ID_BOOTAPP    0x01
#define FLASH_ID_APPL       0x02
#define FLASH_ID_CUSTOMFPGA 0x03

#define FLASH_ID_ROM1541    0x10
#define FLASH_ID_ROM1541C   0x11
#define FLASH_ID_ROM1541II  0x12

#define FLASH_ID_SOUNDS     0x20
#define FLASH_ID_CHARS      0x21
#define FLASH_ID_SIDCRT     0x22

#define FLASH_ID_AR5PAL     0x40
#define FLASH_ID_AR6PAL     0x41
#define FLASH_ID_FINAL3     0x42
#define FLASH_ID_EPYX       0x43
#define FLASH_ID_RR38PAL    0x44
#define FLASH_ID_SS5PAL     0x45
#define FLASH_ID_AR5NTSC    0x46
#define FLASH_ID_RR38NTSC   0x47
#define FLASH_ID_SS5NTSC    0x48
#define FLASH_ID_TAR_PAL    0x49
#define FLASH_ID_TAR_NTSC   0x4A
#define FLASH_ID_ALL_ROMS   0x5F

#define FLASH_ID_CONFIG     0xFE
#define FLASH_ID_LIST_END	0xFF

typedef struct t_flash_address {
	BYTE id;
	BYTE has_header;
	int  start;
	int  device_addr;
	int  max_length;
} _flash_address;


class Flash
{
public:    
    Flash() { }
    ~Flash() { }

	virtual Flash *tester(void) { return NULL; }
    
	// Getting the serial number
    virtual void read_serial(void *buffer) { }

	// Interface for getting images from ROM.
	virtual int  read_image(int image_id, void *buffer, int buf_size) { return 0; }
    virtual void read_linear_addr(int addr, int len, void *buffer) { }
	virtual void read_dev_addr(int device_addr, int len, void *buffer) { } // low level read function, needed for boot
	virtual void get_image_addresses(int image_id, t_flash_address *addr) { }

	// Interface for flashing images
    virtual int  get_number_of_pages(void) { return 1; }
	virtual int  get_page_size(void) { return 1; } // programmable block
    virtual int  get_sector_size(int addr) { return 1; }
    virtual bool erase_sector(int sector) { return false; }
	virtual int  page_to_sector(int page) { return -1; }
    virtual bool read_page(int page, void *buffer) { return false; }
	virtual bool write_page(int page, void *buffer) { return false; }
	virtual bool need_erase(void) { return false; }
	
//    virtual int  write_image(int id, void *buffer) { return 0; }

	// Interface for configuration
    virtual int  get_config_page_size(void) { return 1; }
	virtual int  get_number_of_config_pages(void) { return 0; }
    virtual void read_config_page(int page, int length, void *buffer) { }
    virtual void write_config_page(int page, void *buffer) { }
    virtual void clear_config_page(int page) { }
    
	// Multiple FPGA images
    virtual void reboot(int addr) { }

	// Protection functions
	virtual bool protect_configure(void) { return false; }
	virtual void protect_disable(void) { }
	virtual void protect_enable(void) { }
    virtual void protect_show_status(void) { }

};

Flash *get_flash(void);

/*
class FlashFactory
{
	IndexedList<Flash *> flash_types(8, NULL);
public:
	FlashFactory() { }
	~FlashFactory() { }

	Flash *get_actual_type(void) {
		Flash *retval;
		for(int i=0;i<flash_types.get_elements();i++) {
			retval = flash_types[i]->tester();
				return retval;
		}
		return NULL;
	}
	void   register_type(Flash *t) {
		flash_types.append(t);
	}
};
*/

#endif
