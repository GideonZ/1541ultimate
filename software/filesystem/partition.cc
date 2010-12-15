
#include <stdio.h>
#include "blockdev.h"
#include "partition.h"
#include "fat_fs.h"
#include "iso9660.h"

extern "C" {
    #include "small_printf.h"
}

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
    FileSystem *fs = NULL;
    DWORD sec_size;
    ioctl(GET_SECTOR_SIZE, &sec_size);
    
    bool iso;
    if(sec_size == 2048) { // if 2K, assume ISO9660
        iso = FileSystem_ISO9660 :: check(this);
        if(iso) {
            fs = new FileSystem_ISO9660(this);
            if(!fs->init()) {
                delete fs;
                fs = NULL;
            }
        }
    }    

    if(fs)
        return fs;

    // for quick fix: Assume FATFS:
    BYTE res = FATFS :: check_fs(this);
    printf("Checked FATFS: Result = %d\n", res);
    if(!res) {
        fs = new FATFS(this);
        if(!fs->init()) {
            delete fs;
            fs = NULL;
        }
    }
    return fs;
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
