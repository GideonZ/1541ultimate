#ifndef FILE_DEVICE_H
#define FILE_DEVICE_H

#include "blockdev.h"
#include "disk.h"
#include "partition.h"
#include "path.h"
#include "cached_tree_node.h"

class FileDevice : public CachedTreeNode
{
    BlockDevice *blk;
    Disk *disk;
    const char *display_name;
    bool initialized;
    bool isFloppy;
public:
    FileDevice(BlockDevice *b, const char *n, const char *dispn);
    virtual ~FileDevice();
    
    void setFloppy(void) { isFloppy = true; }
    bool is_ready(void);
    void attach_disk(int block_size);
    void detach_disk(void);
    int  probe(void);
    // int fetch_children(void);
    void get_display_string(char *buffer, int width);
};

#endif
