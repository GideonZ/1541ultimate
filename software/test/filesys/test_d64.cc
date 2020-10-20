/*
 * test.cc
 *
 *  Created on: Sep 1, 2015
 *      Author: Gideon
 */

#include <stdio.h>
#include "blockdev_emul.h"
#include "file_device.h"
#include "dump_hex.h"
#include "fs_errors_flags.h"
#include "directory.h"
#define SS_DEBUG 1

#include "filesystem_d64.h"
#include "side_sectors.h"

#include <stdlib.h>

#define INFO_SIZE 64

FRESULT copy_from(FileSystem *fs, const char *from, const char *to)
{
    File *fi;
    FRESULT fres = fs->file_open(from, FA_READ, &fi);
    uint8_t buffer[1024];
    if (fres == FR_OK) {
        FILE *fo = fopen(to, "wb");
        uint32_t tr;
        do {
            fres = fi->read(buffer, 1024, &tr);
            fwrite(buffer, 1, tr, fo);
        } while(tr);
        fclose(fo);
        fs->file_close(fi);
    }
    return fres;
}

FRESULT verify_file(FileSystem *fs, const char *from, const uint8_t *to, int size)
{
    File *fi;
    FRESULT fres = fs->file_open(from, FA_READ, &fi);
    uint8_t buffer[1024];
    if (fres == FR_OK) {
        uint32_t tr;
        do {
            fres = fi->read(buffer, 1024, &tr);
            if (fres != FR_OK)
                break;
            if (tr) {
                if (memcmp(to, buffer, tr) != 0) {
                    fres = FR_INT_ERR; // verify error
                    break;
                }
            }
            to += tr;
            size -= tr;
        } while(tr && (size > 0));
        fs->file_close(fi);
    }
    if (size != 0) {
        return FR_INT_ERR;
    }
    return fres;
}

FRESULT print_directory(FileSystem *fs, const char *path, FileInfo *reldir = NULL, int indent = 0)
{
    Directory *dir;
    FileInfo info(INFO_SIZE);
    char fatname[48];
    const char *spaces = "                                        ";
    FRESULT fres = fs->dir_open(path, &dir, reldir);
    if (fres == FR_OK) {
        while(1) {
            fres = dir->get_entry(info);
            if (fres != FR_OK)
                break;
            info.generate_fat_name(fatname, 48);
            printf("%s%-5d%-32s %s\n", &spaces[40-indent], info.size / 254, fatname, (info.attrib &AM_DIR)?"DIR ":"FILE");
            //printf("%s%-32s (%s) %10d\n", &spaces[40-indent], info.lfname, (info.attrib &AM_DIR)?"DIR ":"FILE", info.size);

            if (info.attrib & AM_DIR) {
                print_directory(fs, NULL, &info, indent + 2);
            }
        }
        delete dir; // close directory
    }
    if (!indent) {
        uint32_t fre;
        fres = fs->get_free(&fre);
        printf("%u BLOCKS FREE.\n", fre);
    }
    return fres;
}

bool test_fs(FileSystem *fs)
{
    bool ok = true;
    uint8_t *buffer = new uint8_t[65536];
    for(int i=0;i<65536;i++) {
        buffer[i] = (uint8_t)(i >> 1);
    }
    uint32_t tr = 0;
    printf("Formatted:\n");
    print_directory(fs, "");
    uint32_t empty_size;
    FRESULT fres = fs->get_free(&empty_size);
    ok = ok && (fres == FR_OK); // it should be possible to get the number of free sectors.

    File *f;
    fres = fs->file_open("hello.prg", FA_WRITE | FA_CREATE_NEW, &f );
    ok = ok && (fres == FR_OK); // we expect the result to be OK.
    printf("File open result: %s\n", FileSystem::get_error_string(fres));
    if (fres == FR_OK) {
        fres = f->write(buffer, 65536, &tr);
        printf("Write (%u) result: %s\n", tr, FileSystem::get_error_string(fres));
        fs->file_close(f);
    }
    ok = ok && (fres == FR_OK); // we expect the result to be OK.
    ok = ok && (tr == 65536);
    print_directory(fs, "");

    fres = fs->file_rename("hello.prg", "myfile.txt");
    ok = ok && (fres == FR_OK); // we expect the rename to succeed;
    printf("Rename result: %s\n", FileSystem::get_error_string(fres));
    print_directory(fs, "");

    fres = fs->file_delete("hello.prg");
    ok = ok && (fres == FR_NO_FILE); // we expect that the file is not found
    printf("Delete result: %s\n", FileSystem::get_error_string(fres));
    print_directory(fs, "");

    fres = fs->file_delete("myfile.txt");
    ok = ok && (fres == FR_OK); // we expect that the file can be deleted
    printf("Delete result: %s\n", FileSystem::get_error_string(fres));
    print_directory(fs, "");

    fres = fs->file_open("test.seq", FA_WRITE | FA_CREATE_NEW, &f );
    ok = ok && (fres == FR_OK); // we expect that the file can be created

    printf("SEQ File open result: %s\n", FileSystem::get_error_string(fres));
    if (fres == FR_OK) {
        fres = f->write(buffer, 65536, &tr);
        printf("Write (%u) result: %s\n", tr, FileSystem::get_error_string(fres));
        fres = f->write(buffer, 65536, &tr);
        printf("Write (%u) result: %s\n", tr, FileSystem::get_error_string(fres));
        fres = f->write(buffer, 65536, &tr);
        printf("Write (%u) result: %s\n", tr, FileSystem::get_error_string(fres));
        fs->file_close(f);
    }
    print_directory(fs, "");
    uint32_t fre;
    fs->get_free(&fre);
    if (empty_size < 775) {
        ok = ok && (fre == 0);
        ok = ok && (fres == FR_DISK_FULL); // we expect that the disk is full when there are no blocks free
    } else {
        ok = ok && (fres == FR_OK); // if the disk is not full, the file must have been correctly written
        ok = ok && (fre == (empty_size - 775));
    }

    fres = fs->file_open("test.seq", FA_WRITE | FA_CREATE_NEW, &f );
    ok = ok && (fres == FR_EXIST); // since we demand CREATE_NEW, this should fail as the file already exists.
    printf("SEQ File open result second time: %s (%p)\n", FileSystem::get_error_string(fres), f);
    if (f) fs->file_close(f);
    print_directory(fs, "");

    fres = fs->file_open("test.seq", FA_WRITE | FA_CREATE_ALWAYS, &f );
    ok = ok && (fres == FR_OK); // since we demand CREATE_ALWAYS, this should clear the file
    printf("SEQ File open result with always flag: %s (%p)\n", FileSystem::get_error_string(fres), f);
    if (f) {
        fres = f->write(buffer, 10000, &tr);
        printf("Write (%u) result: %s\n", tr, FileSystem::get_error_string(fres));
        fs->file_close(f);
    }
    print_directory(fs, "");

    fs->get_free(&fre);
    ok = ok && (fre == (empty_size - 40)); // 10000 bytes = 40 blocks

    fres = fs->file_open("test.seq", FA_WRITE, &f );
    printf("SEQ File open result with only write flag set: %s (%p)\n", FileSystem::get_error_string(fres), f);
    if (f) {
        printf("Reported size: %d\n", f->get_size());
        fres = f->write("GIDEON", 6, &tr);
        ok = ok && (fres == FR_OK);
        ok = ok && (tr == 6);
        printf("Write (%u) result: %s\n", tr, FileSystem::get_error_string(fres));

        fres = f->seek(100);
        ok = ok && (fres == FR_OK);
        printf("Seek result: %s\n", FileSystem::get_error_string(fres));
        fres = f->write("ZWEIJTZER", 9, &tr);
        ok = ok && (fres == FR_OK);
        ok = ok && (tr == 9);
        printf("Write (%u) result: %s\n", tr, FileSystem::get_error_string(fres));

        fres = f->seek(1012); // going to write in the middle of the file over a block boundary
        ok = ok && (fres == FR_OK);
        printf("Seek result: %s\n", FileSystem::get_error_string(fres));
        fres = f->write("MIDDLE", 6, &tr);
        ok = ok && (fres == FR_OK);
        ok = ok && (tr == 6);
        printf("Write (%u) result: %s\n", tr, FileSystem::get_error_string(fres));

        printf("File Size = %d\n", f->get_size());
        fres = f->seek(f->get_size());
        ok = ok && (fres == FR_OK);
        printf("Seek result: %s\n", FileSystem::get_error_string(fres));
        fres = f->write("APPENDED", 8, &tr);
        ok = ok && (fres == FR_OK);
        ok = ok && (tr == 8);
        printf("Write (%u) result: %s\n", tr, FileSystem::get_error_string(fres));
        printf("Reported size: %d\n", f->get_size());

        fs->file_close(f);
    }
    print_directory(fs, "");

    copy_from(fs, "test.seq", "test.seq");
    memcpy(buffer + 0, "GIDEON", 6);
    memcpy(buffer + 100, "ZWEIJTZER", 9);
    memcpy(buffer + 1012, "MIDDLE", 6);
    memcpy(buffer + 10000, "APPENDED", 8);

    fres = verify_file(fs, "test.seq", buffer, 10008);
    ok = ok && (fres == FR_OK);

    fres = fs->file_delete("test.seq");
    ok = ok && (fres == FR_OK);
    printf("Delete result: %s\n", FileSystem::get_error_string(fres));
    print_directory(fs, "");

    fs->get_free(&fre);
    ok = ok && (fre == empty_size); // disk should be empty again

    printf("Creating BLAHDIR/SECOND...\n");
    fres = fs->dir_create("BLAHDIR/SECOND");
    ok = ok && (fres == FR_NO_PATH); // creating a subdirectory of a directory that doesn't exist should fail.
    printf("Create DIR result: %s\n", FileSystem::get_error_string(fres));
    print_directory(fs, "");

    printf("Creating BLAHDIR...\n");
    fres = fs->dir_create("BLAHDIR");
    ok = ok && (fres == FR_OK); // creating a directory should pass
    printf("Create DIR result: %s\n", FileSystem::get_error_string(fres));
    print_directory(fs, "");

    printf("Creating BLAHDIR/SECOND...\n");
    fres = fs->dir_create("BLAHDIR/SECOND");
    ok = ok && (fres == FR_OK); // creating a sub directory should pass
    printf("Create DIR result: %s\n", FileSystem::get_error_string(fres));
    print_directory(fs, "");

    fs->get_free(&fre);
    ok = ok && (fre == (empty_size - 4)); // 4 blocks should be used for 2 dirs

    fres = fs->file_open("BLAHDIR/SECOND/sub.rel", FA_WRITE | FA_CREATE_NEW, &f );
    ok = ok && (fres == FR_OK); // creating file within a sub-directory should pass
    printf("REL File open result: %s\n", FileSystem::get_error_string(fres));
    if (fres == FR_OK) {
        uint8_t rel_header[2] = { 0x55, 0x00 };
        fres = f->write(rel_header, 2, &tr);
        uint32_t tr = 0;
        fres = f->write(buffer, 63500, &tr); // should be exactly 250 data blocks
        printf("Write (%u) result: %s\n", tr, FileSystem::get_error_string(fres));
        fs->file_close(f);
    }
    print_directory(fs, "");

    int expected_rel_blocks = 250 + 3; // 3 blocks for side sectors
    if (empty_size > 720)
        expected_rel_blocks += 1; // Super side block

    fs->get_free(&fre);
    ok = ok && (fre == (empty_size - 4 - expected_rel_blocks)); // 4 blocks should be used for 2 dirs

    copy_from(fs, "BLAHDIR/SECOND/sub.rel", "sub.rel");

    fs->file_open("BLAHDIR/SECOND/sub.rel", FA_READ, &f);
    if(f) {
        FileInCBM *cbmfile = (FileInCBM *)f->handle;
        cbmfile->dumpSideSectors();
        printf("\nTesting seek within REL file.\n");
        const uint8_t expected_data[] = { 0x55, 0, 0x47, 0x49, 0x44, 0x45, 0x4F, 0x4E, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8 };

        // Seeking within REL file.
        fres = f->read(buffer, 16, &tr);
        dump_hex_relative(buffer, 16);
        ok = ok && (fres == FR_OK);
        ok = ok && (memcmp(buffer, expected_data, 16) == 0);

        fres = f->seek(1);
        ok = ok && (fres == FR_OK);
        fres = f->read(buffer, 16, &tr);
        dump_hex_relative(buffer, 16);
        ok = ok && (fres == FR_OK);
        ok = ok && (memcmp(buffer, expected_data + 1, 16) == 0);

        fres = f->seek(0);
        ok = ok && (fres == FR_OK);
        fres = f->read(buffer, 16, &tr);
        dump_hex_relative(buffer, 16);
        ok = ok && (fres == FR_OK);
        ok = ok && (memcmp(buffer, expected_data, 16) == 0);

        fres = f->seek(2);
        ok = ok && (fres == FR_OK);
        fres = f->read(buffer, 16, &tr);
        dump_hex_relative(buffer, 16);
        ok = ok && (fres == FR_OK);
        ok = ok && (memcmp(buffer, expected_data + 2, 16) == 0);

        fres = f->seek(1014);
        ok = ok && (fres == FR_OK);
        fres = f->read(buffer, 16, &tr);
        dump_hex_relative(buffer, 16);
        ok = ok && (fres == FR_OK);
        ok = ok && (memcmp(buffer, "MIDDLE", 6) == 0);

        fs->file_close(f);
    }

    delete[] buffer;

    return ok;
}

void create_file(const char *filename, int blocks)
{
    uint8_t block[256];
    memset(block, 0, 256);
    FILE *f = fopen(filename, "wb");
    for(int i=0;i<blocks;i++) {
        fwrite(block, 256, 1, f);
    }
    fclose(f);
}

bool test_format_d64()
{
    create_file("format.d64", 683);
    BlockDevice_Emulated blk("format.d64", 256);
    Partition prt(&blk, 0, 683, 0);
    FileSystemD64 fs(&prt, true);
    fs.format("GIDEON,44");
    return test_fs(&fs);
}

bool test_format_d71()
{
    create_file("format.d71", 683*2);
    BlockDevice_Emulated blk("format.d71", 256);
    Partition prt(&blk, 0, 683*2, 0);
    FileSystemD71 fs(&prt, true);
    fs.format("GIDEON,44");
    return test_fs(&fs);
}

bool test_format_d81()
{
    create_file("format.d81", 3200);
    BlockDevice_Emulated blk("format.d81", 256);
    Partition prt(&blk, 0, 3200, 0);
    FileSystemD81 fs(&prt, true);
    fs.format("GIDEON,45");
    return test_fs(&fs);
/*

    SideSectors side(&fs, 100);
    side.test(1441);
    side.dump();

    int t,s;
    side.get_track_sector(t,s);

    SideSectors s2(&fs, 55);
    s2.load(t,s);
    s2.dump();
*/
}

bool test_format_dnp()
{
    create_file("format.dnp", 683);
    BlockDevice *blk = new BlockDevice_Emulated("format.dnp", 256);
    Partition *prt = new Partition(blk, 0, 683, 0);
    FileSystemDNP *fs = new FileSystemDNP(prt, true);
    fs->format("GIDEON,46");
    return test_fs(fs);
}


void read_dnp()
{
    BlockDevice *blk = new BlockDevice_Emulated("storage1.dnp", 256);
    Partition *prt = new Partition(blk, 0, 0, 0);
    uint32_t sectors = 0, free = 0;
    prt->ioctl(GET_SECTOR_COUNT, &sectors);
    printf("Partition number of sectors: %u\n", sectors);
    FileSystemCBM *fs = new FileSystemDNP(prt, true);
    print_directory(fs, "");

    File *f;
    fs->file_open("userlog2.rel", FA_READ, &f);
    if(f) {
        FileInCBM *cbmfile = (FileInCBM *)f->handle;
        cbmfile->dumpSideSectors();
        fs->file_close(f);
    }
    copy_from(fs, "userlog2.rel", "userlog2.rel");
    /*
    PathInfo pi(fs);
    pi.init("scpu demos/supercpukicks/scpu kicks ! dma.prg");
    PathStatus_t st = fs->walk_path(pi);
    printf("Path status = %d\n", st);
*/
}

void read_d81()
{
    BlockDevice *blk = new BlockDevice_Emulated("rel81.d81", 256);
    Partition *prt = new Partition(blk, 0, 0, 0);
    uint32_t sectors = 0, free = 0;
    prt->ioctl(GET_SECTOR_COUNT, &sectors);
    printf("Partition number of sectors: %u\n", sectors);
    FileSystemCBM *fs = new FileSystemD81(prt, true);
    print_directory(fs, "");

    File *f;
    fs->file_open("relfile4.rel", FA_READ, &f);
    if(f) {
        FileInCBM *cbmfile = (FileInCBM *)f->handle;
        cbmfile->dumpSideSectors();
        fs->file_close(f);
    }
}

int main()
{
    bool ok = true;
    //printf("Size of Dir entry: %d\n", sizeof(struct DirEntryCBM));
    //    read_d81();

    //read_dnp();
    ok = test_format_d64();
//    test_format_d71();
//    test_format_d81();
//    test_format_dnp();
    if (!ok) {
        printf("\nTest FAILED.\n");
    } else {
        printf("\nTest PASSED.\n");
    }
}
