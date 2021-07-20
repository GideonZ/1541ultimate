
#include <stdio.h>
#include "blockdev_file.h"

BlockDevice_File::BlockDevice_File(File *file, int sec_size)
{
	this->file = file; // reference only
    shift = 0;
    while(sec_size > 1) {
        shift++;
        sec_size >>= 1;
    }
    if(shift > 12) {
        shift = 12;
    }
    sector_size = (1 << shift);
    file_size = file->get_size();
}
    
BlockDevice_File::~BlockDevice_File()
{
}

DSTATUS BlockDevice_File::init(void)
{
    // We don't need to do anything here, just return status
    return status();
}
        
DSTATUS BlockDevice_File::status(void)
{
    if(file)
        return 0;
    else
        return STA_NODISK;
}
    
DRESULT BlockDevice_File::read(uint8_t *buffer, uint32_t sector, int count)
{
    if(file->seek(sector << shift))
        return RES_PARERR;
        
    uint32_t read;
    if(file->read(buffer, sector_size * (int)count, &read) != FR_OK)
        return RES_ERROR;
    
    return RES_OK;
}

DRESULT BlockDevice_File::write(const uint8_t *buffer, uint32_t sector, int count)
{
    if(file->seek(sector << shift))
        return RES_PARERR;
        
    uint32_t written;
    if(file->write((uint8_t *)buffer, sector_size * (int)count, &written) != FR_OK)
        return RES_ERROR;
    
    return RES_OK;
}

DRESULT BlockDevice_File::ioctl(uint8_t command, void *data)
{
    uint32_t size;
    uint32_t *dest = (uint32_t *)data;
    FRESULT fres = FR_DENIED;
    switch (command) {
        case GET_SECTOR_COUNT:
            if (!file)
                return RES_ERROR;
            size = file_size >> shift;
            *dest = size;
            break;
        case GET_SECTOR_SIZE:
            (*(uint32_t *)data) = sector_size;
            break;
        case GET_BLOCK_SIZE:
            (*(uint32_t *)data) = 1;
            break;
        case CTRL_SYNC:
            if (file) {
                fres = file->sync();
            }
            if (fres != FR_OK) {
                return RES_ERROR;
            }
            break;
        default:
            printf("IOCTL %d.\n", command);
            return RES_PARERR;
    }
    return RES_OK;
}
