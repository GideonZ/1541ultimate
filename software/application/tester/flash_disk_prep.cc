/*
 * flash_disk_prep.cc
 *
 *  Created on: Oct 10, 2021
 *      Author: gideon
 */

#include "blockdev_ram.h"
#include "filemanager.h"
#include "file_device.h"
#include "filesystem_fat.h"

#define ROMS_DIRECTORY  "/prep/roms"
#define CARTS_DIRECTORY "/prep/carts"

// This fragment prepares a flash disk image from binary files in RAM, in order to flash
// it into the DUT as one block, using the standard infrastructure.

extern uint8_t _1541_bin_start;
extern uint8_t _1571_bin_start;
extern uint8_t _1581_bin_start;
extern uint8_t _snds1541_bin_start;
extern uint8_t _snds1571_bin_start;
extern uint8_t _snds1581_bin_start;

BlockDevice *prep_blk;
FileDevice *prep_node;

static FRESULT format_block_dev(BlockDevice *blk, const char *name)
{
    FileSystem *fs;
    Partition *prt;
    prt = new Partition(blk, 0, 0, 0);
    fs  = new FileSystemFAT(prt);
    FRESULT res = fs->format(name);
    delete fs;
    delete prt;
    return res;
}

static void create_dir(const char *name)
{
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->create_dir(name);
    printf("Creating '%s': %s\n", name, FileSystem :: get_error_string(fres));
}

static FRESULT write_file(const char *name, uint8_t *data, int length)
{
    File *f;
    uint32_t dummy;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(ROMS_DIRECTORY, name, FA_CREATE_ALWAYS | FA_WRITE, &f);
    if (fres == FR_OK) {
        fres = f->write(data, length, &dummy);
        printf("Writing %s to /prep: %s\n", name, FileSystem :: get_error_string(fres));
        fm->fclose(f);
    }
    if (fres != FR_OK) {
        printf("Failed to write essentials. Abort!\n");
        while(1)
            ;
    }
    return fres;
}

static void copy_files(void)
{
    create_dir(ROMS_DIRECTORY);
    create_dir(CARTS_DIRECTORY);
    write_file("1581.rom", &_1581_bin_start, 0x8000);
    write_file("1571.rom", &_1571_bin_start, 0x8000);
    write_file("1541.rom", &_1541_bin_start, 0x4000);
    write_file("snds1541.bin", &_snds1541_bin_start, 0xC000);
    write_file("snds1571.bin", &_snds1571_bin_start, 0xC000);
    write_file("snds1581.bin", &_snds1581_bin_start, 0xC000);
}

int prepare_flashdisk_pre(uint8_t *mem, uint32_t mem_size)
{
    prep_blk = new BlockDevice_Ram(mem, 4096, mem_size >> 12);
    format_block_dev(prep_blk, "FlashDisk");

    prep_node = new FileDevice(prep_blk, "prep", "Flash Prep");
    prep_node->attach_disk(4096);
    int a = prep_node->probe();
    if (a > 0) {
        FileManager :: getFileManager()->add_root_entry(prep_node);
    } else {
        return -1;
    }
    return 0;
}

int prepare_flashdisk_used(uint32_t mem_size)
{
    uint32_t free, cs;
    Path p;
    p.cd("/prep");
    FRESULT fres = FileManager :: getFileManager()->get_free(&p, free, cs);
    uint32_t used = 0;
    if (fres == FR_OK) {
        uint32_t blocks = (free * cs) >> 12;
        used = (mem_size >> 12) - blocks;
        printf("Free: %d * %d = %d bytes = %d blocks.\n", free, cs, free * cs, blocks);
        printf("Used blocks: %d\n", used);
    }
    return (int)used;
}

int prepare_flashdisk(uint8_t *mem, uint32_t mem_size)
{
    int pre = prepare_flashdisk_pre(mem, mem_size);
    if (pre) {
        return pre;
    }
    copy_files();
    return prepare_flashdisk_used(mem_size);
}
