
#include "filemanager.h"
#include "init_function.h"

#include "file_device.h"
#include "filesystem_fat.h"
#include "blockdev_ram.h"

uint8_t *ramdisk_mem;
BlockDevice *ramdisk_blk;
FileDevice *ramdisk_node;


void init_ram_disk(void *obj, void *param)
{
    const int sz = 512;
    const int size = 3 * 1024 * 1024;
    const int sectors = size / sz;
    ramdisk_mem = new uint8_t[size];
    ramdisk_blk = new BlockDevice_Ram(ramdisk_mem, sz, sectors);

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
