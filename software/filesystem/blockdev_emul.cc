
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <cerrno>
#include "blockdev_emul.h"
#include "dump_hex.h"

BlockDevice_Emulated::BlockDevice_Emulated(const char *name, int sec_size)
{
    fd = 0;
    test = 12345;
    sector_size = sec_size;
    fd = open(name, O_RDONLY);
    if (fd != -1) {
        file_size = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        set_state(e_device_ready);
		printf("Construct block device, opening file %s of size %ld (%d).\n", name, file_size, fd);
    } else {
        set_state(e_device_no_media);
		printf("Construct block device, (file could not be opened: %s, errno = %d)\n", name, errno);
        fd = 0;
    }
}
    
BlockDevice_Emulated::~BlockDevice_Emulated()
{
    if(fd)
        close(fd);
}

DSTATUS BlockDevice_Emulated::init(void)
{
    // We don't need to do anything here, just return status
    return status();
}
        
DSTATUS BlockDevice_Emulated::status(void)
{
    if(fd)
        return 0;
    else
        return STA_NODISK;
}
    
DRESULT BlockDevice_Emulated::read(uint8_t *buffer, uint32_t sector, int count)
{
    printf("Device read sector %d. (%d)\n", sector, count);
    ssize_t offset = (ssize_t)sector * (ssize_t)sector_size;
    ssize_t read_size = (ssize_t)count * (ssize_t)sector_size;
    ssize_t bytes_read = pread(fd, buffer, read_size, offset);
    if(bytes_read != read_size) {
        return RES_ERROR;
    }
    
    return RES_OK;
}

#if	_READONLY == 0
DRESULT BlockDevice_Emulated::write(const uint8_t *buffer, uint32_t sector, int count)
{
//    printf("Device write sector %d. ", sector);

    ssize_t offset = (ssize_t)sector * (ssize_t)sector_size;
    ssize_t write_size = (ssize_t)count * (ssize_t)sector_size;
    ssize_t written = pwrite(fd, buffer, write_size, offset);
    if(written != write_size)
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
            if(!fd)
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
