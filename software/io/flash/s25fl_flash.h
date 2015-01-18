#ifndef S25FL_FLASH_H
#define S25FL_FLASH_H

#include "integer.h"
#include "flash.h"
#include "w25q_flash.h"
#include "iomap.h"

#define S25FL_PAGE_CONFIG_START (8192-256)
#define S25FL_NUM_CONFIG_PAGES  16

#define S25FL_ContinuousArrayRead_LowFrequency       0x03
#define S25FL_ContinuousArrayRead_HighFrequency      0x0B
#define S25FL_JEDEC_ID					            0x9F
#define S25FL_SectorErase                            0x20 // 4K
#define S25FL_BlockErase_64K                         0xD8 // 
#define S25FL_ChipErase                              0xC7 //,94H,80H,9AH
#define S25FL_WriteEnable							0x06
#define S25FL_WriteDisable							0x04
#define S25FL_ReadStatusRegister1					0x05
#define S25FL_ReadStatusRegister2					0x35
#define S25FL_WriteStatusRegister					0x01
#define S25FL_PageProgram							0x02

#define SPI_FLASH_DATA     *((volatile BYTE *)(FLASH_BASE + 0x00))
#define SPI_FLASH_DATA_32  *((volatile DWORD*)(FLASH_BASE + 0x00))
#define SPI_FLASH_CTRL     *((volatile BYTE *)(FLASH_BASE + 0x08))

#define SPI_FORCE_SS 0x01
#define SPI_LEVEL_SS 0x02

class S25FL_Flash : public W25Q_Flash
{
    int sector_size;
	int sector_count;
    int total_size;
	
public:    
    S25FL_Flash();
    virtual ~S25FL_Flash();

	virtual Flash *tester(void);
    
	// Protection functions
	virtual void protect_disable(void);
	virtual bool protect_configure(void);
	virtual void protect_enable(void);
};

extern S25FL_Flash s25fl_flash;

#endif
