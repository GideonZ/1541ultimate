#ifndef FILE_PARTITION_H
#define FILE_PARTITION_H

#include "blockdev.h"
#include "disk.h"
#include "file_system.h"
#include "partition.h"
#include "cached_tree_node.h"
#include "path.h"


class FilePartition : public CachedTreeNode
{
	Partition *prt;
    
    void init(void);
public:
    FilePartition(CachedTreeNode *par, Partition *p, char *n);
    virtual ~FilePartition();

    const char *get_type_string(uint8_t typ);
    void  get_display_string(char *buffer, int width);
//    int   fetch_children(void);
};

#endif
