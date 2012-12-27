#ifndef BLOCKDEV_RAM_H
#define BLOCKDEV_RAM_H

#include "blockdev.h"
#include "file_system.h"
#include "file.h"

class BlockDevice_Ram : public BlockDevice
{
    BYTE *memory;
    int number_of_sectors;
    int sector_size;
public:
    BlockDevice_Ram(BYTE *mem, int sec_size, int num_sectors);
    ~BlockDevice_Ram();

    virtual DSTATUS init(void);
    virtual DSTATUS status(void);
    virtual DRESULT read(BYTE *, DWORD, BYTE);
    virtual DRESULT write(const BYTE *, DWORD, BYTE);
    virtual DRESULT ioctl(BYTE, void *);
};

#endif
