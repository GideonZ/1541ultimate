#ifndef FLASH_H
#define FLASH_H

#include "integer.h"
#include "indexed_list.h"

#define FLASH_ID_BOOTFPGA   0x00
#define FLASH_ID_BOOTAPP    0x01
#define FLASH_ID_APPL       0x02
//#define FLASH_ID_CUSTOMFPGA 0x03

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
#define FLASH_ID_KCS	    0x4B
#define FLASH_ID_CUSTOM_ROM 0x4C
#define FLASH_ID_KERNAL_ROM 0x4D
#define FLASH_ID_CUSTOM_DRV 0x4E
#define FLASH_ID_ORIG_KERNAL 0x4F
#define FLASH_ID_BASIC_ROM  0x50
#define FLASH_ID_CHARGEN_ROM 0x51
#define FLASH_ID_ORIG_BASIC 0x52
#define FLASH_ID_ORIG_CHARGEN 0x53

#define FLASH_ID_ALL_ROMS   0x5F

#define FLASH_ID_CONFIG     0xFE
#define FLASH_ID_LIST_END	0xFF

typedef struct t_flash_address {
	uint8_t id;
	uint8_t has_header;
	int  start;
	int  device_addr;
	int  max_length;
} _flash_address;

class Flash
{
public:    
	static IndexedList<Flash*>* getSupportedFlashTypes() {
    	static IndexedList<Flash*> flash_types(4, NULL);
    	return &flash_types;
    }

	Flash() {
		getSupportedFlashTypes()->append(this);
	}
    virtual ~Flash() { }

	virtual Flash *tester(void) { return NULL; }
    
	// Getting the serial number
    virtual const char *get_type_string(void) { return "Base"; }
	virtual void read_serial(void *buffer) { }

	// Interface for getting images from ROM.
	virtual int  read_image(int image_id, void *buffer, int buf_size) { return 0; }
    virtual void read_linear_addr(int addr, int len, void *buffer) { }
	virtual void read_dev_addr(int device_addr, int len, void *buffer) { } // low level read function, needed for boot
	virtual void get_image_addresses(int image_id, t_flash_address *addr) { }

	// Interface for flashing images
    virtual int  get_number_of_pages(void) { return 1; }
	virtual int  get_page_size(void) { return 512; } // programmable block
    virtual int  get_sector_size(int addr) { return 1; }
    virtual bool erase_sector(int sector) { return false; }
	virtual int  page_to_sector(int page) { return -1; }
    virtual bool read_page(int page, void *buffer) { return false; }
	virtual bool write_page(int page, void *buffer) { return false; }
	virtual bool need_erase(void) { return false; }
	int  write_image(int id, uint8_t *buffer, int length);

	// Interface for configuration
    virtual int  get_config_page_size(void) { return 512; }
	virtual int  get_number_of_config_pages(void) { return 0; }
    virtual void read_config_page(int page, int length, void *buffer) {
        char *dest = (char *)buffer;
        for (int i=0;i<length;i++) 
            *(dest++) = (char)-1;
    }
        
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

#endif
