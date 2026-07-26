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
#define HTML_DIRECOTRY "/prep/html"
#define CFG_DIRECTORY  "/prep/config"

// This fragment prepares a flash disk image from binary files in RAM, in order to flash
// it into the DUT as one block, using the standard infrastructure.

extern uint8_t _1541_bin_start;
extern uint8_t _1571_bin_start;
extern uint8_t _1581_bin_start;
extern uint8_t _snds1541_bin_start;
extern uint8_t _snds1571_bin_start;
extern uint8_t _snds1581_bin_start;
extern uint8_t _characters_901225_01_bin_start;
extern uint8_t _kernal_901227_03_bin_start;
extern uint8_t _basic_901226_01_bin_start;
extern uint8_t _index_html_start[];
extern uint8_t _index_html_end[1];
extern uint8_t _iec_config_bin_start[];
extern uint8_t _iec_config_bin_end[1];

BlockDevice *prep_blk;
FileDevice *prep_node;

static FRESULT format_block_dev(BlockDevice *blk, const char *name)
{
    FileSystem *fs;
    Partition *prt;
    prt = new Partition(blk, 0, 0, 0);
    fs  = new FileSystemFAT(prt);
    FRESULT res = fs->format(name);
    printf("Formatting '%s': %s\n", name, FileSystem :: get_error_string(res));
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

static FRESULT write_file(const char *dir, const char *name, uint8_t *data, int length)
{
    File *f;
    uint32_t dummy;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(dir, name, FA_CREATE_ALWAYS | FA_WRITE, &f);
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
    create_dir(HTML_DIRECOTRY);
    create_dir(CFG_DIRECTORY);
    write_file(ROMS_DIRECTORY, "1581.rom", &_1581_bin_start, 0x8000);
    write_file(ROMS_DIRECTORY, "1571.rom", &_1571_bin_start, 0x8000);
    write_file(ROMS_DIRECTORY, "1541.rom", &_1541_bin_start, 0x4000);
    write_file(ROMS_DIRECTORY, "snds1541.bin", &_snds1541_bin_start, 0xC000);
    write_file(ROMS_DIRECTORY, "snds1571.bin", &_snds1571_bin_start, 0xC000);
    write_file(ROMS_DIRECTORY, "snds1581.bin", &_snds1581_bin_start, 0xC000);
//    write_file(ROMS_DIRECTORY, "kernal.bin", &_kernal_901227_03_bin_start, 0x2000);
//    write_file(ROMS_DIRECTORY, "basic.bin", &_basic_901226_01_bin_start, 0x2000);
//    write_file(ROMS_DIRECTORY, "chars.bin", &_characters_901225_01_bin_start, 0x1000);
    write_file(HTML_DIRECOTRY, "index.html", (uint8_t *)_index_html_start, (long int)_index_html_end - (long int)_index_html_start);
    write_file(CFG_DIRECTORY, "iec_partitions.ipr", (uint8_t *)_iec_config_bin_start, (long int)_iec_config_bin_end - (long int)_iec_config_bin_start);
}

int prepare_flashdisk_pre(uint8_t *mem, uint32_t mem_size)
{
    prep_blk = new BlockDevice_Ram(mem, 4096, mem_size >> 12);
    if (format_block_dev(prep_blk, "FlashDisk") != FR_OK) {
        return -2;
    }
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

void cleanup()
{
    FileManager :: getFileManager()->remove_root_entry(prep_node);
    delete prep_node;
    delete prep_blk;
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

void create(const uint32_t mem_size, const char *fn)
{
    uint8_t *mem = new uint8_t[mem_size]; // from w25q_flash.cc
    int used = prepare_flashdisk(mem, mem_size);
    printf("Used blocks: %d\n", used);
    FILE *fo = fopen(fn, "wb");    
    if (fo) {
        fwrite(mem, 4096, used, fo);
        fclose(fo);
    }
    delete[] mem;
    cleanup();
}

int main(int argc, char **argv)
{
    create(0xBE8000, "fat_50t.bin");
    create(0xA68000, "fat_100t.bin");
}
