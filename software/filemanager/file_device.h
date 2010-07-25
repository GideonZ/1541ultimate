#ifndef FILE_DEVICE_H
#define FILE_DEVICE_H

#include "blockdev.h"
#include "disk.h"
#include "partition.h"
#include "path.h"

class FileDevice : public PathObject
{
    BlockDevice *blk;
    Disk *disk;
public:
    FileDevice(PathObject *p, BlockDevice *b, char *n);
    virtual ~FileDevice();
    
    int fetch_children(void);
    char *get_display_string(void);
};

#endif
