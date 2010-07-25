#ifndef SPIFLASH_H
#define SPIFLASH_H

#include "integer.h"

#define FLASH_ADDR_BOOTAPP    0x053CA0
#define FLASH_ADDR_AR5PAL     0x063000
#define FLASH_ADDR_AR6PAL     0x06B000
#define FLASH_ADDR_FINAL3     0x073000
#define FLASH_ADDR_SOUNDS     0x083000
#define FLASH_ADDR_CHARS      0x08A000
#define FLASH_ADDR_SIDCRT     0x08B000
#define FLASH_ADDR_EPYX       0x08D000
#define FLASH_ADDR_ROM1541    0x08F000
#define FLASH_ADDR_RR38PAL    0x093000
#define FLASH_ADDR_SS5PAL     0x0A3000
#define FLASH_ADDR_AR5NTSC    0x0B3000
#define FLASH_ADDR_ROM1541C   0x0BB000
#define FLASH_ADDR_ROM1541II  0x0BF000
#define FLASH_ADDR_RR38NTSC   0x0C3000
#define FLASH_ADDR_SS5NTSC    0x0D3000
#define FLASH_ADDR_TAR_PAL    0x0E3000
#define FLASH_ADDR_TAR_NTSC   0x0F3000
// Free space 0x103000 - 108000 (0x5000)
#define FLASH_ADDR_APPL       0x108000 // start of sector 8


#define FLASH_DEVADDR_BOOTAPP ((FLASH_ADDR_BOOTAPP / 528) << 10)
#define FLASH_DEVADDR_APPL    ((FLASH_ADDR_APPL / 528) << 10)

#define FLASH_ADDR_CONFIG_START 0x20DF00
#define FLASH_PAGE_CONFIG_START 4080
#define FLASH_NUM_CONFIG_PAGES  16

#define FLASH_MainMemoryPageRead                            0xD2
#define FLASH_ContinuousArrayRead_LegacyCommand             0xE8
#define FLASH_ContinuousArrayRead_LowFrequency              0x03
#define FLASH_ContinuousArrayRead_HighFrequency             0x0B
#define FLASH_Buffer1Read_LowFrequency                      0xD1
#define FLASH_Buffer2Read_LowFrequency                      0xD3
#define FLASH_Buffer1Read                                   0xD4
#define FLASH_Buffer2Read                                   0xD6
#define FLASH_Buffer1Write                                  0x84
#define FLASH_Buffer2Write                                  0x87
#define FLASH_Buffer1toMainMemoryPageProgramWithErase       0x83
#define FLASH_Buffer2toMainMemoryPageProgramWithErase       0x86
#define FLASH_Buffer1toMainMemoryPageProgramWithoutErase    0x88
#define FLASH_Buffer2toMainMemoryPageProgramWithoutErase    0x89
#define FLASH_PageErase                                     0x81
#define FLASH_BlockErase                                    0x50
#define FLASH_SectorErase                                   0x7C
#define FLASH_ChipErase                                     0xC7 //,94H,80H,9AH
#define FLASH_MainMemoryPageProgramThroughBuffer1           0x82
#define FLASH_MainMemoryPageProgramThroughBuffer2           0x85
#define FLASH_MainMemoryPagetoBuffer1Transfer               0x53
#define FLASH_MainMemoryPagetoBuffer2Transfer               0x55
#define FLASH_MainMemoryPagetoBuffer1Compare                0x60
#define FLASH_MainMemoryPagetoBuffer2Compare                0x61
#define FLASH_AutoPageRewritethroughBuffer1                 0x58
#define FLASH_AutoPageRewritethroughBuffer2                 0x59
#define FLASH_DeepPowerdown                                 0xB9
#define FLASH_ResumefromDeepPowerdown                       0xAB
#define FLASH_StatusRegisterRead                            0xD7
#define FLASH_ManufacturerandDeviceIDRead                   0x9F
#define FLASH_ReadSecurityRegister                          0x77

#define FLASH_DATA     *((volatile BYTE *)0x4060200)
#define FLASH_DATA_32  *((volatile DWORD*)0x4060200)
#define FLASH_CTRL     *((volatile BYTE *)0x4060208)

#define SPI_FORCE_SS 0x01
#define SPI_LEVEL_SS 0x02

class SpiFlash
{
    int page_size;
    int block_size;
    int sector_size;
    int total_size;
    int page_shift;

    void wait_ready(int time_out);
public:    
    SpiFlash();
    ~SpiFlash();
    
    void read(int addr, int len, void *buffer);
    void read_devaddr(int addr, int len, void *buffer);
    void write(int addr, int len, void *buffer);
    void erase_sector(int addr);
    void read_serial(void *buffer);

    void reboot(int addr);
    int  get_page_size(void);
    void read_page(int page, void *buffer);
    void write_page(int page, void *buffer);
};

extern SpiFlash flash; // global access

#endif
