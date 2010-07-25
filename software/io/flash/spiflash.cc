#include "spiflash.h"
#include "icap.h"
#include "small_printf.h"


#ifdef BOOTLOADER
#define debug(x)
#define error(x)
#else
#define debug(x)
#define error(x) printf x
#endif

//#define debug(x)

#ifndef FIRST_WRITABLE_PAGE
#   define FIRST_WRITABLE_PAGE 768
#endif


SpiFlash flash; // global access

SpiFlash::SpiFlash()
{
    page_size   = 528;  // in bytes
    block_size  = 8;    // in pages
    sector_size = 256;  // in pages
    total_size  = 4096; // in pages
    
    page_shift  = 10;   // offset in address reg

    FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    FLASH_DATA = 0xFF;
}
    
SpiFlash::~SpiFlash()
{
    debug(("Spi Flash destructed..\n"));
}

int SpiFlash :: get_page_size(void)
{
    return page_size;
}
    
void SpiFlash :: read(int addr, int len, void *buffer)
{
    int page = addr / page_size;
    int offset = addr - (page * page_size);
    int device_addr = (page << page_shift) | offset;
    
    debug(("Requested addr: %6x. Page: %d. Offset = %d. ADDR: %6x\n", addr, page, offset, device_addr));
    debug(("%b %b %b\n", BYTE(device_addr >> 16), BYTE(device_addr >> 8), BYTE(device_addr)));
    read_devaddr(device_addr, len, buffer);
}

void SpiFlash :: read_devaddr(int device_addr, int len, void *buffer)
{
    FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    FLASH_DATA = FLASH_ContinuousArrayRead_LowFrequency;
    FLASH_DATA = BYTE(device_addr >> 16);
    FLASH_DATA = BYTE(device_addr >> 8);
    FLASH_DATA = BYTE(device_addr);

    BYTE *buf = (BYTE *)buffer;
    for(int i=0;i<len;i++) {
        *(buf++) = FLASH_DATA;
    }
    FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
}
    

void SpiFlash :: write(int addr, int len, void *buffer)
{
    int page = addr / page_size;
    int offset = addr - (page * page_size);
    int device_addr = (page << page_shift) | offset;
    BYTE *buf = (BYTE *)buffer;
    
    if(page < FIRST_WRITABLE_PAGE) {
        error(("Error: Trying to write to a protected page (%d).\n", page));
        return;
    }

    debug(("Requested addr: %6x. Page: %d. Offset = %d. ADDR: %6x\n", addr, page, offset, device_addr));
    debug(("%b %b %b\n", BYTE(device_addr >> 16), BYTE(device_addr >> 8), BYTE(device_addr)));
    FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    FLASH_DATA = FLASH_MainMemoryPageProgramThroughBuffer1;
    FLASH_DATA = BYTE(device_addr >> 16);
    FLASH_DATA = BYTE(device_addr >> 8);
    FLASH_DATA = BYTE(device_addr);
    for(int i=0;i<len;i++) {
        FLASH_DATA = *(buf++);
    }
    FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    wait_ready(5000);
}

void SpiFlash :: wait_ready(int time_out)
{
// TODO: FOR Microblaze or any faster CPU, we need to insert wait cycles here
    int i=time_out;
    FLASH_CTRL = SPI_FORCE_SS;
    FLASH_DATA = FLASH_StatusRegisterRead;
    while(!(FLASH_DATA & 0x80)) {
        if(!(i--)) {
            debug(("Flash timeout.\n"));
            break;
        }
    }
    FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
}
    
void SpiFlash :: read_serial(void *buffer)
{
    BYTE *buf = (BYTE *)buffer;
    FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    FLASH_DATA = FLASH_ReadSecurityRegister;
    FLASH_DATA = 0;
    FLASH_DATA = 0;
    FLASH_DATA = 0;
    for(int i=0;i<64;i++) {
        FLASH_DATA = 0xFF; // read dummy
    }
    for(int i=0;i<64;i++) {
        *(buf++) = FLASH_DATA;
    }
    FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
}

void SpiFlash :: read_page(int page, void *buffer)
{
    int device_addr = (page << page_shift);
    int len = (page_size >> 2);
    DWORD *buf = (DWORD *)buffer;
    
    FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    FLASH_DATA = FLASH_ContinuousArrayRead_LowFrequency;
    FLASH_DATA = BYTE(device_addr >> 16);
    FLASH_DATA = BYTE(device_addr >> 8);
    FLASH_DATA = BYTE(device_addr);
    for(int i=0;i<len;i++) {
        *(buf++) = FLASH_DATA_32;
    }
    FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
}

void SpiFlash :: write_page(int page, void *buffer)
{
    if(page < FIRST_WRITABLE_PAGE) {
        error(("Error: Trying to write to a protected page (%d).\n", page));
        return;
    }

    int device_addr = (page << page_shift);
    int len = (page_size >> 2);
    DWORD *buf = (DWORD *)buffer;
    
    FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    FLASH_DATA = FLASH_MainMemoryPageProgramThroughBuffer2;
    FLASH_DATA = BYTE(device_addr >> 16);
    FLASH_DATA = BYTE(device_addr >> 8);
    FLASH_DATA = BYTE(device_addr);
    for(int i=0;i<len;i++) {
        FLASH_DATA_32 = *(buf++);
    }
    FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    wait_ready(5000);
}

void SpiFlash :: erase_sector(int addr)
{
    int page = addr / page_size;
    int device_addr = (page << page_shift);
    
    debug(("Sector erase. ADDR: %6x\n", addr));
    debug(("%b %b %b\n", BYTE(device_addr >> 16), BYTE(device_addr >> 8), BYTE(device_addr)));
    FLASH_CTRL = SPI_FORCE_SS; // drive CSn low
    FLASH_DATA = FLASH_SectorErase;
    FLASH_DATA = BYTE(device_addr >> 16);
    FLASH_DATA = BYTE(device_addr >> 8);
    FLASH_DATA = BYTE(device_addr);
    FLASH_CTRL = SPI_FORCE_SS | SPI_LEVEL_SS;
    wait_ready(50000);
}

void SpiFlash :: reboot(int addr)
{
    int page = addr / page_size;
    int offset = addr - (page * page_size);
    int device_addr = (page << page_shift) | offset;

    BYTE icap_sequence[20] = { 0xAA, 0x99, 0x32, 0x61,    0,    0, 0x32, 0x81,    0,    0,
                               0x32, 0xA1, 0x00, 0x4F, 0x30, 0xA1, 0x00, 0x0E, 0x20, 0x00 };

    icap_sequence[4] = (device_addr >> 8);
    icap_sequence[5] = (device_addr >> 0);
    icap_sequence[9] = (device_addr >> 16);
    icap_sequence[8] = FLASH_ContinuousArrayRead_LowFrequency;

    ICAP_PULSE = 0;
    ICAP_PULSE = 0;
    for(int i=0;i<20;i++) {
        ICAP_WRITE = icap_sequence[i];
    }
    ICAP_PULSE = 0;
    ICAP_PULSE = 0;
    printf("You should never see this!\n");        
}
