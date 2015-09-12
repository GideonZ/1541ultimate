/*-----------------------------------------------------------------------
/  Partition Interface
/-----------------------------------------------------------------------*/
#ifndef BLOCKDEV_EMUL_H
#define BLOCKDEV_EMUL_H
#include "blockdev.h"

class BlockDevice_Emulated : public BlockDevice
{
    FILE *f;
    long file_size;
    int sector_size;
public:
    BlockDevice_Emulated(const char *name, int sec_size);
    virtual ~BlockDevice_Emulated();

    virtual DSTATUS init(void);
    virtual DSTATUS status(void);
    virtual DRESULT read(uint8_t *, uint32_t, int);
#if	_READONLY == 0
    virtual DRESULT write(const uint8_t *, uint32_t, int);
#endif
    virtual DRESULT ioctl(uint8_t, void *);
    
};

#endif
