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
#include "directory.h"
#define SS_DEBUG 1

#include "filesystem_fat.h"
#include "filesystem_d64.h"

#include "side_sectors.h"

#include <stdlib.h>

#define INFO_SIZE 64

FRESULT copy_from(FileSystem *fs, const char *from, const char *to)
{
    printf("Copying %s to %s.\n", from, to);
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
        fi->close();
    } else {
        printf("File '%s' not found.\n", from);
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
        fi->close();
    } else {
        printf("File '%s' not found.\n", from);
    }
    if (size != 0) {
        return FR_INT_ERR;
    }
    return fres;
}

FRESULT copy_to(const char *from, FileSystem *fs, const char *to)
{
    printf("Copying %s to %s.\n", from, to);
    File *fo;

    FILE *fi = fopen(from, "rb");
    if (!fi) {
        return FR_NO_FILE;
    }

    uint8_t *buffer = new uint8_t[16384];
    FRESULT fres = fs->file_open(to, FA_WRITE | FA_CREATE_ALWAYS, &fo);
    if (fres == FR_OK) {
        uint32_t tr;
        do {
            tr = fread(buffer, 1, 16384, fi);
            fres = fo->write(buffer, tr, &tr);
        } while(tr);
        fclose(fi);
        fo->close();
    } else {
        printf("File '%s' could not be opened for writing. (%s)\n", to, FileSystem::get_error_string(fres));
    }
    delete[] buffer;
    return fres;
}

FRESULT print_directory(FileSystem *fs, const char *path, int indent = 0)
{
    Directory *dir;
    FileInfo info(INFO_SIZE);
    char fatname[48];
    const char *spaces = "                                        ";
    FRESULT fres = fs->dir_open(path, &dir);
    if (fres == FR_OK) {
        while(1) {
            fres = dir->get_entry(info);
            if (fres != FR_OK)
                break;
            info.generate_fat_name(fatname, 48);
            printf("%s%-5d%-32s %s (%02x)\n", &spaces[40-indent], info.size / 254, fatname, (info.attrib &AM_DIR)?"DIR ":"FILE", info.attrib);
            //printf("%s%-32s (%s) %10d\n", &spaces[40-indent], info.lfname, (info.attrib &AM_DIR)?"DIR ":"FILE", info.size);

            if (info.attrib & AM_DIR) {
                mstring deeperPath;
                deeperPath = path;
                deeperPath += "/";
                deeperPath += info.lfname;
                //print_directory(fs, deeperPath.c_str(), indent + 2);
            }
        }
        delete dir; // close directory
    } else {
        printf("Directory '%s' cannot be opened: %s\n", path, FileSystem::get_error_string(fres));
    }
    if (!indent) {
        uint32_t fre, cs;
        fres = fs->get_free(&fre, &cs);
        printf("%u BLOCKS FREE.\n", fre);
    }
    return fres;
}

FRESULT copy_all(FileSystem *fs)
{
    Directory *dir;
    FileInfo info(INFO_SIZE);
    char fatname[48];
    FRESULT fres = FR_OK;
    FRESULT dres = fs->dir_open("", &dir);
    if (dres == FR_OK) {
        while(1) {
            dres = dir->get_entry(info);
            if (dres != FR_OK)
                break;
            if (info.attrib & (AM_DIR | AM_VOL)) {
                continue;
            }
            info.generate_fat_name(fatname, 48);
            fres = copy_from(fs, fatname, fatname);
            if (fres != FR_OK) {
                break;
            }
        }
        delete dir; // close directory
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
    uint32_t empty_size, cs;
    FRESULT fres = fs->get_free(&empty_size, &cs);
    ok = ok && (fres == FR_OK); // it should be possible to get the number of free sectors.

    File *f;
    fres = fs->file_open("hello.prg", FA_WRITE | FA_CREATE_NEW, &f );
    ok = ok && (fres == FR_OK); // we expect the result to be OK.
    printf("File open result: %s\n", FileSystem::get_error_string(fres));
    if (fres == FR_OK) {
        fres = f->write(buffer, 65536, &tr);
        printf("Write (%u) result: %s\n", tr, FileSystem::get_error_string(fres));
        f->close();
    }
    ok = ok && (fres == FR_OK); // we expect the result to be OK.
    ok = ok && (tr == 65536);
    print_directory(fs, "");

    fres = fs->file_open("hello.prg", FA_READ, &f);
    ok = ok && (fres == FR_OK); // we expect the result to be OK.
    uint32_t sz = f->get_size();
    printf("Size of hello: %d\n", sz);
    ok = ok & (sz == 65536);
    f->close();

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
        f->close();
    }
    print_directory(fs, "");
    uint32_t fre;
    fs->get_free(&fre, &cs);
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
    if (f) f->close();
    print_directory(fs, "");

    fres = fs->file_open("test.seq", FA_WRITE | FA_CREATE_ALWAYS, &f );
    ok = ok && (fres == FR_OK); // since we demand CREATE_ALWAYS, this should clear the file
    printf("SEQ File open result with always flag: %s (%p)\n", FileSystem::get_error_string(fres), f);
    if (f) {
        fres = f->write(buffer, 10000, &tr);
        printf("Write (%u) result: %s\n", tr, FileSystem::get_error_string(fres));
        f->close();
    }
    print_directory(fs, "");

    fs->get_free(&fre, &cs);
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

        f->close();
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

    fs->get_free(&fre, &cs);
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

    fs->get_free(&fre, &cs);
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
        f->close();
    }
    print_directory(fs, "");

    int expected_rel_blocks = 250 + 3; // 3 blocks for side sectors
    if (empty_size > 720)
        expected_rel_blocks += 1; // Super side block

    fs->get_free(&fre, &cs);
    ok = ok && (fre == (empty_size - 4 - expected_rel_blocks)); // 4 blocks should be used for 2 dirs

    copy_from(fs, "BLAHDIR/SECOND/sub.rel", "sub.rel");

    fs->file_open("BLAHDIR/SECOND/sub.rel", FA_READ, &f);
    if(f) {
        FileInCBM *cbmfile = (FileInCBM *)f;
        cbmfile->dumpSideSectors();
        uint32_t sz = f->get_size();
        ok = ok && (sz == 63502);
        printf("\nTesting size of REL: %d\n", sz);
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

        f->close();
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
    BlockDevice_Emulated blk("format.dnp", 256);
    Partition prt(&blk, 0, 683, 0);
    FileSystemDNP fs(&prt, true);
    fs.format("GIDEON,46");
    return test_fs(&fs);
}

bool test_format_fat()
{
    create_file("format.fat", 4*2*1024); // 2 MB
    BlockDevice_Emulated blk("format.fat", 512);
    Partition prt(&blk, 0, 0, 0);
    FileSystemFAT fs(&prt);
    fs.format("GIDEON");
    if (!fs.init()) {
        printf("Initialization of FAT file system failed.\n");
        return false;
    }
    return test_fs(&fs);
}

void read_dnp()
{
    BlockDevice *blk = new BlockDevice_Emulated("FOSIE_MAIN.dnp", 256);
    Partition *prt = new Partition(blk, 0, 0, 0);
    uint32_t sectors = 0, free = 0;
    prt->ioctl(GET_SECTOR_COUNT, &sectors);
    printf("Partition number of sectors: %u\n", sectors);
    FileSystemCBM *fs = new FileSystemDNP(prt, true);
    print_directory(fs, "");

#if 0
    File *f;
/*
    fs->file_open("userlog2.rel", FA_READ, &f);
    if(f) {
        FileInCBM *cbmfile = (FileInCBM *)f;
        cbmfile->dumpSideSectors();
        f->close();
    }
    copy_from(fs, "userlog2.rel", "userlog2.rel");
*/


    uint8_t *buffer = new uint8_t[65536];
    for(int i=0;i<65536;i++) {
        buffer[i] = (uint8_t)(i >> 1);
    }
    fs->file_open("blah.rel", FA_CREATE_NEW, &f);
    if (f) {
        uint32_t tr;
        FRESULT fres;
        uint16_t temp = 200;
        fres = f->write(&temp, 2, &tr);
        printf("%s\n", FileSystem::get_error_string(fres));
        fres = f->write(buffer, 65536, &tr);
        printf("%s\n", FileSystem::get_error_string(fres));
        fres = f->write(buffer, 65536, &tr);
        printf("%s\n", FileSystem::get_error_string(fres));
        fres = f->write(buffer, 65536, &tr);
        printf("%s\n", FileSystem::get_error_string(fres));
        fres = f->write(buffer, 65536, &tr);
        printf("%s\n", FileSystem::get_error_string(fres));
        f->close();
    }
    delete[] buffer;
    print_directory(fs, "");

#endif

    delete fs;
    delete prt;
    delete blk;
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
        FileInCBM *cbmfile = (FileInCBM *)f;
        cbmfile->dumpSideSectors();
        f->close();
    }

    delete fs;
    delete prt;
    delete blk;
}

bool read_cvt(char *fn, char *file)
{
    //BlockDevice_Emulated blk("megapatch/mp33r6-en.d81", 256);
    BlockDevice_Emulated blk(fn, 256);
    Partition prt(&blk, 0, 0, 0);
    FileSystemD81 fs(&prt, false);

    print_directory(&fs, "");
    if (file) {
        FRESULT fres = copy_from(&fs, file, file);
        if (fres != FR_OK) {
            return false;
        }
    } else {
        FRESULT fres = copy_all(&fs);
        if (fres != FR_OK) {
            printf("ERR.\n");
            return false;
        }
    }
    return true;
}

// To be moved to its own target, to test FAT specific things
void test_fat()
{
	//BlockDevice_Emulated blk("fat/exfat.fat", 512);
    BlockDevice_Emulated blk("fingerprint.fat", 512);
    Partition prt(&blk, 0, 0, 0);
    FileSystemFAT fs(&prt);
    if (!fs.init()) {
        printf("Initialization of FAT file system failed.\n");
        return;
    }
    print_directory(&fs, "");

    //print_directory(&fs, "/main.dnp/utilities");
    //print_directory(&fs, "/megapatch/mp33r6-en.d81/blah");
}

bool test1(int argc, char **argv)
{
    bool ok = true;

    ok &= test_format_d64();
    ok &= test_format_d71();
    ok &= test_format_d81();
    ok &= test_format_dnp();
    //ok &= test_format_fat();

    return ok;
}

bool test2(int argc, char **argv)
{
	if (argc < 1) {
		printf("Test 2 requires at least one argument.\n");
		return false;
	}
	return read_cvt(argv[0], (argc > 1) ? argv[1] : 0);
}

bool test3(int argc, char **argv)
{
	if (argc < 1) {
		printf("Test 3 requires at least one argument.\n");
		return false;
	}
	char filename[80];
	strncpy(filename, argv[0], 75);
	strcat(filename, ".d81");
	create_file(filename, 3200);
    BlockDevice_Emulated blk(filename, 256);
    Partition prt(&blk, 0, 3200, 0);
    FileSystemD81 fs(&prt, true);
    fs.format("TEST3,03");

    for(int i=1; i < argc; i++) {
    	FRESULT fres = copy_to(argv[i], &fs, argv[i]);
    	if (fres != FR_OK) {
    		return false;
    	}
    }
    return true;
}

bool test4(int argc, char **argv)
{
	if (argc < 1) {
		printf("Test 4 requires at least one argument.\n");
		return false;
	}

	BlockDevice *blk = new BlockDevice_Emulated(argv[0], 256);
    Partition *prt = new Partition(blk, 0, 0, 0);
    uint32_t sectors = 0, free = 0;
    prt->ioctl(GET_SECTOR_COUNT, &sectors);
    printf("Partition number of sectors: %u\n", sectors);
    FileSystemCBM *fs = new FileSystemD81(prt, true);
    print_directory(fs, "");

    {
        Directory *dir;
        FileInfo info(INFO_SIZE);
        char fatname[48];
        FRESULT fres = fs->dir_open(NULL, &dir);
        if (fres == FR_OK) {
            while(1) {
                fres = dir->get_entry(info);
                if (fres != FR_OK)
                    break;
                info.generate_fat_name(fatname, 48);

                fres = fs->file_delete(fatname);
                printf("Deleting '%s' => %s\n", fatname, FileSystem::get_error_string(fres));
            }
            delete dir; // close directory
        }
    }

    print_directory(fs, "");
    delete fs;
    delete prt;
    delete blk;
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
    	printf("Usage: %s <testcase> [testcase params]\n", argv[0]);
    	printf("Testcase 1: Deep test of formatting D64, D71, D81, DNP, writing files,\n");
        printf("            checking them, etc.. No params.\n");
    	printf("Testcase 2: CVT Extraction test. <image file> [specific file to extract].\n");
    	printf("            If file not given, it extracts all into current directory.\n");
    	printf("Testcase 3: CVT Writeback test. <base name> [files]\n");
    	printf("            Creates a D81 file, and copies all specified files into it.\n");
    	printf("Testcase 4: Delete all files from disk image and show resulting");
		printf("            free blocks.\n");
    	exit(0);
    }

    int testcase = atoi(argv[1]);
    argc -= 2;
    argv++;
    argv++;

    bool ok = true;
    switch(testcase) {
    case 1:
    	ok = test1(argc, argv);
    	break;
    case 2:
    	ok = test2(argc, argv);
    	break;
    case 3:
    	ok = test3(argc, argv);
    	break;
    case 4:
    	ok = test4(argc, argv);
    	break;
    default:
    	printf("Undefined test.\n");
    	ok = false;
    }
    // read_dnp();
    // test_fat();
    // return 0;
    if (!ok) {
        printf("\nTest FAILED.\n");
        return 1;
    } else {
        printf("\nTest PASSED.\n");
        return 0;
    }
}
