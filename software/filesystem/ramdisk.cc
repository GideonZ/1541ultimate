
#include "filemanager.h"
#include "init_function.h"

#include "file_device.h"
#include "filesystem_fat.h"
#include "blockdev_ram.h"

// uint8_t *ramdisk_mem;
BlockDevice *ramdisk_blk;
FileDevice *ramdisk_node;

extern uint8_t __ram_disk_start;
extern uint8_t __ram_disk_limit;

void init_ram_disk(void *obj, void *param)
{
    const int sz = 512;
#ifdef U2
    const int size = 1024 * 1024;
    const int sectors = size / sz;
    uint8_t *ramdisk_mem = new uint8_t[size]; // 1 MB only
    ramdisk_blk = new BlockDevice_Ram(ramdisk_mem, sz, sectors);
#else
    const int size = (int)&__ram_disk_limit - (int)&__ram_disk_start; // 3 * 1024 * 1024;
    const int sectors = size / sz;
    ramdisk_blk = new BlockDevice_Ram(&__ram_disk_start, sz, sectors);
#endif
    {
        FileSystem *ramdisk_fs;
        Partition *ramdisk_prt;
        ramdisk_prt = new Partition(ramdisk_blk, 0, 0, 0);
        ramdisk_fs  = new FileSystemFAT(ramdisk_prt);
        ramdisk_fs->format("RamDisk");
        delete ramdisk_fs;
        delete ramdisk_prt;
    }
    ramdisk_node = new FileDevice(ramdisk_blk, "Temp", "RAM Disk");
    ramdisk_node->attach_disk(512);
    FileManager :: getFileManager()->add_root_entry(ramdisk_node);
}

InitFunction ramdisk_init(init_ram_disk, NULL, NULL);
