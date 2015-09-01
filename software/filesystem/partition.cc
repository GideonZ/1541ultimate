
#include <stdio.h>
#include "blockdev.h"
#include "partition.h"
#include "iso9660.h"
#include "globals.h"

#include <stdio.h>

Partition::Partition(BlockDevice *blk, uint32_t offset, uint32_t size, uint8_t t)
{
	next_partition = NULL;

	dev    = blk;
    start  = offset;
    length = size;
    type   = t;

    printf("Created partition at offset %d, size = %d, type = %d.\n", offset, size, t);
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
	FileSystem *fs = Globals :: getFileSystemFactory() -> create(this);
	if(fs) {
		if (fs->init()) {
			return fs;
		}
		delete fs;
	}
	return NULL;
}
    
DRESULT Partition::read(uint8_t *buffer, uint32_t sector, uint8_t count)
{
	if(!dev)
        return RES_NOTRDY;
    return dev->read(buffer,start + sector,count);
}

#if	_READONLY == 0
DRESULT Partition::write(const uint8_t *buffer, uint32_t sector, uint8_t count)
{
    if(!dev)
        return RES_NOTRDY;
//    printf("Write sector %d (%d)\n", sector,count);
    return dev->write(buffer,start + sector,count);
}
#endif

DRESULT Partition::ioctl(uint8_t command, void *data)
{
	if(command == CTRL_SYNC) {
		return RES_OK; // we don't implement any caches yet
	}
	if(command == GET_SECTOR_COUNT) {
        *((uint32_t *)data) = length;
        return RES_OK;
    }
    if(!dev)
        return RES_NOTRDY;
    return dev->ioctl(command, data);
}
