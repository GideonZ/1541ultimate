#ifndef BLOCKDEV_RAM_H
#define BLOCKDEV_RAM_H

#include "blockdev.h"
#include "file_system.h"
#include "file.h"

class BlockDevice_Ram : public BlockDevice
{
    uint8_t *memory;
    int number_of_sectors;
    int sector_size;
public:
    BlockDevice_Ram(uint8_t *mem, int sec_size, int num_sectors);
    ~BlockDevice_Ram();

    virtual DSTATUS init(void);
    virtual DSTATUS status(void);
    virtual DRESULT read(uint8_t *, uint32_t, int);
    virtual DRESULT write(const uint8_t *, uint32_t, int);
    virtual DRESULT ioctl(uint8_t, void *);
};

#endif
