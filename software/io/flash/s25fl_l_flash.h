#ifndef S25FL_FLASH_H
#define S25FL_FLASH_H

#include "integer.h"
#include "flash.h"
#include "w25q_flash.h"
#include "iomap.h"

#define S25FLL_PageShift	8  // (256 bytes per page)

#define S25FLL_JEDEC_ID					            0x9F
#define S25FLL_ReadUniqueID							0x4B

// Read
#define S25FLL_Read							        0x03
#define S25FLL_Read32								0x13
#define S25FLL_FastRead						        0x0B
#define S25FLL_FastRead32							0x0C

// Write & Erase
#define S25FLL_PageProgram							0x02
#define S25FLL_PageProgram32						0x12
#define S25FLL_SectorErase                          0x20 // 4K
#define S25FLL_SectorErase32                        0x21 // 4K
#define S25FLL_HalfBlockErase 						0x52
#define S25FLL_HalfBlockErase32						0x53
#define S25FLL_BlockErase	 						0xD8
#define S25FLL_BlockErase32							0xDC
#define S25FLL_ChipErase                            0x60 //,94H,80H,9AH

// Status & Configuration
#define S25FLL_ReadStatusRegister1					0x05
#define S25FLL_ReadStatusRegister2					0x07
#define S25FLL_ReadConfigurationRegister1			0x35
#define S25FLL_ReadConfigurationRegister2			0x15
#define S25FLL_ReadConfigurationRegister3			0x33
#define S25FLL_ReadAnyRegister						0x65

#define S25FLL_WriteEnable							0x06
#define S25FLL_WriteDisable							0x04
#define S25FLL_WriteRegisters						0x01
#define S25FLL_WriteAnyRegister						0x71


#define SPI_FLASH_DATA     *((volatile uint8_t *)(FLASH_BASE + 0x00))
#define SPI_FLASH_DATA_32  *((volatile uint32_t*)(FLASH_BASE + 0x00))
#define SPI_FLASH_CTRL     *((volatile uint8_t *)(FLASH_BASE + 0x08))

#define SPI_FORCE_SS 0x01
#define SPI_LEVEL_SS 0x02

class S25FLxxxL_Flash : protected W25Q_Flash
{
public:    
    S25FLxxxL_Flash();
    virtual ~S25FLxxxL_Flash();

	virtual Flash *tester(void);
	virtual const char *get_type_string(void);
    
	// Protection functions
	virtual void protect_disable(void);
	virtual bool protect_configure(void);
	virtual void protect_enable(void);

	// Low level read/write/erase
	virtual bool erase_sector(int sector);
	virtual bool read_page(int page, void *buffer);
	virtual bool write_page(int page, void *buffer);

	static uint8_t write_config_register(uint8_t *was);
};

extern S25FLxxxL_Flash s25flxxxl_flash;

#endif
