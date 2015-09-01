/*-----------------------------------------------------------------------
/  Partition Interface
/-----------------------------------------------------------------------*/
#ifndef PARTITION_H
#define PARTITION_H

#define _READONLY	0	/* 1: Read-only mode */
#define _USE_IOCTL	1

#include <stdint.h>
#include "blockdev.h"
//#include "file_system.h"

class FileSystem;

class Partition
{
private:
    BlockDevice *dev;
    uint32_t   start;
    uint32_t   length;
    uint8_t    type;
public:
    Partition   *next_partition;

    /* constructor */
    Partition(BlockDevice *blk, uint32_t offset, uint32_t size, uint8_t type);
    ~Partition();                 /* destructor */
    
    void print_info(void);
    uint8_t get_type(void) { return type; }
    FileSystem *attach_filesystem(void);
    
    // Fall through:
    DSTATUS status(void);
    DRESULT read(uint8_t *, uint32_t, uint8_t);
#if	_READONLY == 0
    DRESULT write(const uint8_t *, uint32_t, uint8_t);
#endif
    DRESULT ioctl(uint8_t, void *);
};

#endif
