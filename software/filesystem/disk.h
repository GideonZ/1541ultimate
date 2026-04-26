#ifndef DISK_H
#define DISK_H

#include "blockdev.h"
#include "partition.h"

#define BS_Signature		510
#define BS_FilSysType		54
#define BS_FilSysType32		82
#define MBR_PTable			446

#define GPT_SIGNATURE                0x00
#define GPT_REVISION                 0x08
#define GPT_PARTITION_ARRAY_LBA_LO   0x48
#define GPT_PARTITION_ARRAY_LBA_HI   0x4c
#define GPT_PARTITION_ARRAY_COUNT    0x50
#define GPT_PARTITION_ENTRY_SIZE     0x54

#define GPT_ENTRY_TYPE_GUID          0x00
#define GPT_ENTRY_FIRST_LBA_LO       0x20
#define GPT_ENTRY_FIRST_LBA_HI       0x24
#define GPT_ENTRY_LAST_LBA_LO        0x28
#define GPT_ENTRY_LAST_LBA_HI        0x2c
#define GPT_ENTRY_ATTRIBUTES         0x30

#define BASIC_DATA_ATTRIBUTE7_NO_DRIVE_LETTER  0x80
#define BASIC_DATA_ATTRIBUTE7_HIDDEN           0x40
#define BASIC_DATA_ATTRIBUTE7_SHADOW_COPY      0x20
#define BASIC_DATA_ATTRIBUTE7_READ_ONLY        0x10

// If any of the below partition attributes are present we will ignore the
// partition. Under Windows NO_DRIVE_LETTER sometimes seems to be set even when
// a partition does have a drive letters assigned, so we ignore that attribute.
#define GPT_PARTITION_IGNORE_ATTRIBUTES (\
  BASIC_DATA_ATTRIBUTE7_HIDDEN | \
  BASIC_DATA_ATTRIBUTE7_SHADOW_COPY | \
  BASIC_DATA_ATTRIBUTE7_READ_ONLY)

// The only GUID we need to support (holds all FAT type file systems)
#define GUID_MICROSOFT_BASIC_DATA "\xA2\xA0\xD0\xEB\xE5\xB9\x33\x44\x87\xC0\x68\xB6\xB7\x26\x99\xC7"


class Disk
{
    int              sector_size;
    int              p_count;
    BlockDevice     *dev;
    uint8_t         *buf;
    Partition       *last_partition;
    
    int read_ebr(uint32_t lba);
    void register_partition(uint32_t start, uint32_t size, uint8_t parttype);
public:
    Partition       *partition_list;

    Disk(BlockDevice *b, int sec_size); /* constructor */
    ~Disk();                            /* destructor  */

    int Init(bool isFloppy);            /* Initialize disk, read structures */
};

#endif
