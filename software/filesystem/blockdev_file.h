#ifndef BLOCKDEV_FILE_H
#define BLOCKDEV_FILE_H

#include "blockdev.h"
#include "file_system.h"
#include "file.h"
#include "filemanager.h"

class BlockDevice_File : public BlockDevice
{
    File *file;
    int file_size;
    int sector_size;
    int shift;
public:
    BlockDevice_File(CachedTreeNode *obj, int sec_size);
    ~BlockDevice_File();

    virtual DSTATUS init(void);
    virtual DSTATUS status(void);
    virtual DRESULT read(uint8_t *, uint32_t, int);
    virtual DRESULT write(const uint8_t *, uint32_t, int);
    virtual DRESULT ioctl(uint8_t, void *);
};

#endif
