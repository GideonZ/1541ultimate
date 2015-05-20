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
    BlockDevice     *dev;
    int              sector_size;
    uint8_t            *buf;
    int              p_count;
    
    int read_ebr(Partition ***prt_list, uint32_t lba);
    
public:
    Partition       *partition_list;

    Disk(BlockDevice *b, int sec_size); /* constructor */
    ~Disk();                            /* destructor  */

    int Init(void);             /* Initialize disk, read structures */
};

#endif
