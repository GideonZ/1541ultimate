#ifndef AT49_FLASH_H
#define AT49_FLASH_H

#include "integer.h"
#include "flash.h"

#define AT49_PAGE_CONFIG_START 1
#define AT49_NUM_CONFIG_PAGES  7

#define AT49_BASE         0x2000000
#define AT49_MEM_ARRAY(x) (*(volatile BYTE *)(AT49_BASE + x))
#define AT49_COMMAND1     (*(volatile BYTE *)(AT49_BASE + 0x0AAA))
#define AT49_COMMAND2     (*(volatile BYTE *)(AT49_BASE + 0x1554))

class AT49_Flash : public Flash
{
    int page_size;
	int sector_count;
    int total_size;

	BYTE last_status;
    bool wait_ready(int time_out);
    int sector_to_addr(int sector);
public:    
    AT49_Flash();
    ~AT49_Flash();

	virtual AT49_Flash *tester(void);

	// Getting the serial number
    virtual void read_serial(void *buffer);

	// Interface for getting images from ROM.
	virtual int  read_image(int image_id, void *buffer, int buf_size);
    virtual void read_linear_addr(int addr, int len, void *buffer);

	virtual void get_image_addresses(int image_id, t_flash_address *addr);
	virtual void read_dev_addr(int device_addr, int len, void *buffer);

	// Interface for flashing images
	virtual int  get_page_size(void);
    virtual int  get_sector_size(int addr);
    virtual bool erase_sector(int sector);
	virtual int  page_to_sector(int page);
	virtual bool write_page(int page, void *buffer);
	virtual bool need_erase(void) { return true; }

	// Interface for configuration
    virtual int  get_config_page_size(void);
	virtual int  get_number_of_config_pages(void);
    virtual void read_config_page(int page, int length, void *buffer);
    virtual void write_config_page(int page, void *buffer);

	// Multiple FPGA images
    virtual void reboot(int addr);
    
	// Protection functions
	virtual void protect_disable(void);
	virtual bool protect_configure(void);
	virtual void protect_enable(void);
};

extern AT49_Flash at49_flash;

#endif
