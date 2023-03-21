#ifndef DISK_H
#define DISK_H

#include "blockdev.h"
#include "partition.h"

#define BS_Signature		510
#define BS_FilSysType		54
#define BS_FilSysType32		82
#define MBR_PTable			446

class Disk
{
    int              sector_size;
    int              p_count;
    BlockDevice     *dev;
    uint8_t         *buf;
    Partition       *last_partition;
    
    int read_ebr(uint32_t lba);
public:
    Partition       *partition_list;

    Disk(BlockDevice *b, int sec_size); /* constructor */
    ~Disk();                            /* destructor  */

    int Init(bool isFloppy);            /* Initialize disk, read structures */
};

#endif
