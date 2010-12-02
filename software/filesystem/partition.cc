
#include <stdio.h>
#include "blockdev.h"
#include "partition.h"
#include "small_printf.h"

Partition::Partition(BlockDevice *blk, DWORD offset, DWORD size, BYTE t)
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
	printf("*** PARTITION NOW GONE.. ***\n");
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
    
DRESULT Partition::read(BYTE *buffer, DWORD sector, BYTE count)
{
    if(!dev)
        return RES_NOTRDY;
    return dev->read(buffer,start + sector,count);
}

#if	_READONLY == 0
DRESULT Partition::write(const BYTE *buffer, DWORD sector, BYTE count)
{
    if(!dev)
        return RES_NOTRDY;
//    printf("Write sector %d (%d)\n", sector,count);
    return dev->write(buffer,start + sector,count);
}
#endif

DRESULT Partition::ioctl(BYTE command, void *data)
{
	if(command == CTRL_SYNC) {
		return RES_OK; // we don't implement any caches yet
	}
	if(command == GET_SECTOR_COUNT) {
        *((DWORD *)data) = length;
        return RES_OK;
    }
        
    if(!dev)
        return RES_NOTRDY;
    return dev->ioctl(command, data);
}
