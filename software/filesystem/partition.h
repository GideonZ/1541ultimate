/*-----------------------------------------------------------------------
/  Partition Interface
/-----------------------------------------------------------------------*/
#ifndef PARTITION_H
#define PARTITION_H

#define _READONLY	0	/* 1: Read-only mode */
#define _USE_IOCTL	1

#include "integer.h"
#include "blockdev.h"
//#include "file_system.h"

class FileSystem;

class Partition
{
private:
    BlockDevice *dev;
    DWORD   start;
    DWORD   length;
    BYTE    type;
public:
    Partition   *next_partition;

    /* constructor */
    Partition(BlockDevice *blk, DWORD offset, DWORD size, BYTE type);
    ~Partition();                 /* destructor */
    
    void print_info(void);
    BYTE get_type(void) { return type; }
    FileSystem *attach_filesystem(void);
    
    // Fall through:
    DSTATUS status(void);
    DRESULT read(BYTE *, DWORD, BYTE);
#if	_READONLY == 0
    DRESULT write(const BYTE *, DWORD, BYTE);
#endif
    DRESULT ioctl(BYTE, void *);
};

#endif
