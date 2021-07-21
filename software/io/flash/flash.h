#ifndef FLASH_H
#define FLASH_H

#include "integer.h"
#include "indexed_list.h"

#define FLASH_ID_BOOTFPGA   0x00
#define FLASH_ID_BOOTAPP    0x01
#define FLASH_ID_APPL       0x02
#define FLASH_ID_FLASHDRIVE 0xFD
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
    virtual bool read_page_power2(int page, void *buffer) { return read_page(page, buffer); }
	virtual bool write_page(int page, const void *buffer) { return false; }
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
