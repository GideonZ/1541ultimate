#ifndef W25Q_FLASH_H
#define W25Q_FLASH_H

#include "integer.h"
#include "flash.h"
#include "iomap.h"

#define W25Q_NUM_CONFIG_PAGES  16

#define W25Q_ContinuousArrayRead_LowFrequency       0x03
#define W25Q_ContinuousArrayRead_HighFrequency      0x0B
#define W25Q_JEDEC_ID					            0x9F
#define W25Q_ReadUniqueIDNumber						0x4B
#define W25Q_SectorErase                            0x20 // 4K
#define W25Q_BlockErase_32K                         0x52 // 
#define W25Q_BlockErase_64K                         0xD8 // 
#define W25Q_ChipErase                              0xC7 //,94H,80H,9AH
#define W25Q_WriteEnable							0x06
#define W25Q_WriteDisable							0x04
#define W25Q_ReadStatusRegister1					0x05
#define W25Q_ReadStatusRegister2					0x35
#define W25Q_WriteStatusRegister1					0x01
#define W25Q_WriteStatusRegister2					0x31
#define W25Q_WriteStatusRegister3					0x11
#define W25Q_PageProgram							0x02

#define SPI_FLASH_DATA     *((volatile uint8_t *)(FLASH_BASE + 0x00))
#define SPI_FLASH_DATA_32  *((volatile uint32_t*)(FLASH_BASE + 0x00))
#define SPI_FLASH_CTRL     *((volatile uint8_t *)(FLASH_BASE + 0x08))

#define SPI_FORCE_SS 0x01
#define SPI_LEVEL_SS 0x02

#define W25Q_PageShift	8

class W25Q_Flash : public Flash
{
protected:
	int sector_size;
	int sector_count;
    int total_size;

public:    
    W25Q_Flash();
    virtual ~W25Q_Flash();

	virtual Flash *tester(void);

	// Getting the serial number
    virtual void read_serial(void *buffer);
    virtual const char *get_type_string(void);

	// Interface for getting images from ROM.
	virtual int  read_image(int image_id, void *buffer, int buf_size);
    virtual void read_linear_addr(int addr, int len, void *buffer);
	virtual void get_image_addresses(int image_id, t_flash_address *addr);
	virtual void read_dev_addr(int device_addr, int len, void *buffer);

	// Interface for flashing images
	virtual int  get_page_size(void);
    virtual int  get_number_of_pages(void) { return total_size; }
    virtual int  get_sector_size(int addr);
    virtual bool erase_sector(int sector);
	virtual int  page_to_sector(int page);
	virtual bool read_page(int page, void *buffer);
	virtual bool write_page(int page, void *buffer);
	virtual bool need_erase(void) { return true; }

	// Interface for configuration
    virtual int  get_config_page_size(void);
	virtual int  get_number_of_config_pages(void);
    virtual void read_config_page(int page, int length, void *buffer);
    virtual void write_config_page(int page, void *buffer);
    virtual void clear_config_page(int page);

	// Multiple FPGA images
    virtual void reboot(int addr);
    
	// Protection functions
	virtual void protect_disable(void);
	virtual bool protect_configure(void);
	virtual void protect_enable(void);

    // internal, yet inheritable
    virtual bool wait_ready(int time_out);
};

extern W25Q_Flash w25q_flash;

#endif
