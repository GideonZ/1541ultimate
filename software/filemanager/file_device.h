#ifndef FILE_DEVICE_H
#define FILE_DEVICE_H

#include "blockdev.h"
#include "disk.h"
#include "partition.h"
#include "path.h"
#include "file_direntry.h"

class FileDevice : public FileDirEntry
{
    BlockDevice *blk;
    Disk *disk;
public:
    FileDevice(PathObject *p, BlockDevice *b, char *n);
    virtual ~FileDevice();
    
    void attach_disk(int block_size);
    void detach_disk(void);
    int fetch_children(void);
    char *get_display_string(void);
};

#endif
