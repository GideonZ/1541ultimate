#ifndef BLOCKDEV_FILE_H
#define BLOCKDEV_FILE_H

#include "blockdev.h"
#include "file_system.h"
#include "file.h"

class BlockDevice_File : public BlockDevice
{
    File *file;
    int file_size;
    int sector_size;
    int shift;
public:
    BlockDevice_File(PathObject *obj, int sec_size);
    ~BlockDevice_File();

    virtual DSTATUS init(void);
    virtual DSTATUS status(void);
    virtual DRESULT read(BYTE *, DWORD, BYTE);
    virtual DRESULT write(const BYTE *, DWORD, BYTE);
    virtual DRESULT ioctl(BYTE, void *);
};

#endif
