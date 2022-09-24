
#include <stdio.h>
#include "blockdev.h"
#include "partition.h"
#include "file_system.h"

#include <stdio.h>

Partition::Partition(BlockDevice *blk, uint32_t offset, uint32_t size, uint8_t t)
{
	next_partition = NULL;

	dev    = blk;
    start  = offset;
    length = size;
    type   = t;

    if (!size) {
        blk->ioctl(GET_SECTOR_COUNT, &length);
    }
    //printf("Created partition at offset %d, size = %d, type = %d.\n", offset, size, t);
}
    
Partition::~Partition()
{
	// printf("*** PARTITION NOW GONE.. ***\n");
	//    dev = 0;
}

void Partition :: print_info(void)
{
    printf("Device: %p\nOffset: %d\nLength: %d\nType:%d\n", dev, start, length, type);
}

DSTATUS Partition::status(void)
{
    if(!dev)
        return STA_NOINIT;
    return dev->status();
}

FileSystem *Partition :: attach_filesystem(void)
{
	FileSystem *fs = FileSystem :: getFileSystemFactory() -> create(this);
	if(fs) {
		if (fs->init()) {
			return fs;
		}
		delete fs;
	}
	return NULL;
}
    
DRESULT Partition::read(uint8_t *buffer, uint32_t sector, int count)
{
	if(!dev)
        return RES_NOTRDY;
	if (sector >= length)
	    return RES_PARERR;
	return dev->read(buffer,start + sector,count);
}

#if	_READONLY == 0
DRESULT Partition::write(const uint8_t *buffer, uint32_t sector, int count)
{
    if(!dev)
        return RES_NOTRDY;
    if (sector >= length)
        return RES_PARERR;
    return dev->write(buffer,start + sector,count);
}
#endif

DRESULT Partition::ioctl(uint8_t command, void *data)
{
    /*
     // Let's just forward the sync command, as we do not yet implement caches.
     if(command == CTRL_SYNC) {
     return RES_OK;
     }
     */
    if (command == GET_SECTOR_COUNT) {
        *((uint32_t *)data) = length;
        return RES_OK;
    }
    if (!dev)
        return RES_NOTRDY;
    return dev->ioctl(command, data);
}
