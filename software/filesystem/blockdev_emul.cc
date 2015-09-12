
#include <stdio.h>
#include <sys/stat.h>
#include "blockdev_emul.h"

BlockDevice_Emulated::BlockDevice_Emulated(const char *name, int sec_size)
{
    sector_size = sec_size;
    struct stat file_status;
    if(!stat(name, &file_status)) {
        file_size = file_status.st_size;
        f = fopen(name, "rb+");
        set_state(e_device_ready);
		printf("Construct block device, opening file %s of size %ld (%p).\n", name, file_size, f);
    } else {
        set_state(e_device_no_media);
		printf("Construct block device, fstat failed (file not found: %s)\n", name);
        f = NULL;
    }
}
    
BlockDevice_Emulated::~BlockDevice_Emulated()
{
    if(f)
        fclose(f);
}

DSTATUS BlockDevice_Emulated::init(void)
{
    // We don't need to do anything here, just return status
    return status();
}
        
DSTATUS BlockDevice_Emulated::status(void)
{
    if(f)
        return 0;
    else
        return STA_NODISK;
}
    
DRESULT BlockDevice_Emulated::read(uint8_t *buffer, uint32_t sector, int count)
{
//    printf("Device read sector %d.\n", sector);

    if(fseek(f, sector * sector_size, SEEK_SET))
        return RES_PARERR;
        
    int read = fread(buffer, sector_size, (int)count, f);
    if(read != (int)count)
        return RES_ERROR;
    
    return RES_OK;
}

#if	_READONLY == 0
DRESULT BlockDevice_Emulated::write(const uint8_t *buffer, uint32_t sector, int count)
{
//    printf("Device write sector %d. ", sector);

    if(fseek(f, sector * sector_size, SEEK_SET))
        return RES_PARERR;
        
    int written = fwrite(buffer, sector_size, (int)count, f);
//    printf("%d\n", written);
    if(written != (int)count)
        return RES_ERROR;
    
    return RES_OK;
}
#endif

DRESULT BlockDevice_Emulated::ioctl(uint8_t command, void *data)
{
    uint32_t size;
    uint32_t *dest = (uint32_t *)data;
    
    switch(command) {
        case GET_SECTOR_COUNT:
            if(!f)
                return RES_ERROR;
            size = file_size / sector_size;
            *dest = size;
            break;
        case GET_SECTOR_SIZE:
            (*(uint32_t *)data) = sector_size;
            break;
        case GET_BLOCK_SIZE:
            (*(uint32_t *)data) = 128*1024;
            break;
        default:
            printf("IOCTL %d.\n", command);
            return RES_PARERR;
    }
    return RES_OK;
}
