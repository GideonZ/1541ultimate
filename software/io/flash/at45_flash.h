#ifndef AT45_FLASH_H
#define AT45_FLASH_H

#include "integer.h"
#include "flash.h"

#define AT45_PAGE_CONFIG_START 4080
#define AT45_NUM_CONFIG_PAGES  16

#define AT45_MainMemoryPageRead                            0xD2
#define AT45_ContinuousArrayRead_LegacyCommand             0xE8
#define AT45_ContinuousArrayRead_LowFrequency              0x03
#define AT45_ContinuousArrayRead_HighFrequency             0x0B
#define AT45_Buffer1Read_LowFrequency                      0xD1
#define AT45_Buffer2Read_LowFrequency                      0xD3
#define AT45_Buffer1Read                                   0xD4
#define AT45_Buffer2Read                                   0xD6
#define AT45_Buffer1Write                                  0x84
#define AT45_Buffer2Write                                  0x87
#define AT45_Buffer1toMainMemoryPageProgramWithErase       0x83
#define AT45_Buffer2toMainMemoryPageProgramWithErase       0x86
#define AT45_Buffer1toMainMemoryPageProgramWithoutErase    0x88
#define AT45_Buffer2toMainMemoryPageProgramWithoutErase    0x89
#define AT45_PageErase                                     0x81
#define AT45_BlockErase                                    0x50
#define AT45_SectorErase                                   0x7C
#define AT45_ChipErase                                     0xC7 //,94H,80H,9AH
#define AT45_MainMemoryPageProgramThroughBuffer1           0x82
#define AT45_MainMemoryPageProgramThroughBuffer2           0x85
#define AT45_MainMemoryPagetoBuffer1Transfer               0x53
#define AT45_MainMemoryPagetoBuffer2Transfer               0x55
#define AT45_MainMemoryPagetoBuffer1Compare                0x60
#define AT45_MainMemoryPagetoBuffer2Compare                0x61
#define AT45_AutoPageRewritethroughBuffer1                 0x58
#define AT45_AutoPageRewritethroughBuffer2                 0x59
#define AT45_DeepPowerdown                                 0xB9
#define AT45_ResumefromDeepPowerdown                       0xAB
#define AT45_StatusRegisterRead                            0xD7
#define AT45_ManufacturerandDeviceIDRead                   0x9F
#define AT45_ReadSecurityRegister                          0x77

#define SPI_FLASH_DATA     *((volatile BYTE *)0x4060200)
#define SPI_FLASH_DATA_32  *((volatile DWORD*)0x4060200)
#define SPI_FLASH_CTRL     *((volatile BYTE *)0x4060208)

#define SPI_FORCE_SS 0x01
#define SPI_LEVEL_SS 0x02

class AT45_Flash : public Flash
{
    int page_size;
    int block_size;
    int sector_size;
	int sector_count;
    int total_size;
    int page_shift;

	BYTE last_status;
    bool wait_ready(int time_out);

public:    
    AT45_Flash();
    ~AT45_Flash();

	virtual AT45_Flash *tester(void);

	// Getting the serial number
    virtual void read_serial(void *buffer);

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
	virtual bool need_erase(void) { return false; }


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
    virtual void protect_show_status(void);
};

extern AT45_Flash at45_flash;

#endif
