
extern "C" {
    #include "small_printf.h"
}
#include "blockdev_ram.h"
#include "filemanager.h"

BlockDevice_Ram::BlockDevice_Ram(uint8_t *mem, int sec_size, int num_sectors)
{
    memory = mem;
    sector_size = sec_size;
    number_of_sectors = num_sectors;    
}
    
BlockDevice_Ram::~BlockDevice_Ram()
{
}

DSTATUS BlockDevice_Ram::init(void)
{
    // We don't need to do anything here, just return status
    return status();
}
        
DSTATUS BlockDevice_Ram::status(void)
{
    if(memory)
        return 0;
    else
        return STA_NODISK;
}
    
DRESULT BlockDevice_Ram::read(uint8_t *buffer, uint32_t sector, int count)
{
    if(sector >= number_of_sectors)
        return RES_PARERR;
    
    memcpy(buffer, &memory[sector * sector_size], count*sector_size);
    return RES_OK;
}

DRESULT BlockDevice_Ram::write(const uint8_t *buffer, uint32_t sector, int count)
{
    if(sector >= number_of_sectors)
        return RES_PARERR;
        
    memcpy(&memory[sector * sector_size], buffer, count*sector_size);
    return RES_OK;
}

DRESULT BlockDevice_Ram::ioctl(uint8_t command, void *data)
{
    uint32_t size;
    uint32_t *dest = (uint32_t *)data;
    
    switch(command) {
        case GET_SECTOR_COUNT:
            if(!memory)
                return RES_ERROR;
            *dest = number_of_sectors;
            break;
        case GET_SECTOR_SIZE:
            *dest = sector_size;
            break;
        case GET_BLOCK_SIZE:
            *dest = sector_size; // dummy
            break;
        default:
            printf("IOCTL %d.\n", command);
            return RES_PARERR;
    }
    return RES_OK;
}
