/*
 * test_mounts.cc
 *
 *  Created on: Oct 31, 2020
 *      Author: Gideon
 */

#include <stdio.h>
#include "partition.h"
#include "filemanager.h"
#include "filesystem_dummy.h"
#include "filesystem_fat.h"
#include "filesystem_iso9660.h"
#include "node_directfs.h"
#include "blockdev_emul.h"
#include "file_device.h"
#include "disk.h"
#include "dump_hex.h"

FRESULT copy_from(const char *from, const char *to)
{
    printf("Copying %s to %s.\n", from, to);
    File *fi;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(from, FA_READ, &fi);
    uint8_t buffer[1024];
    if (fres == FR_OK) {
        FILE *fo = fopen(to, "wb");
        uint32_t tr;
        do {
            fres = fi->read(buffer, 1024, &tr);
            //printf("%d\n", tr);
            fwrite(buffer, 1, tr, fo);
        } while(tr);
        fclose(fo);
        fm->fclose(fi);
    } else {
        printf("File '%s' not found. (%s)\n", from, FileSystem::get_error_string(fres));
    }
    return fres;
}

FRESULT copy_to(const char *from, const char *to)
{
    printf("Copying %s to %s.\n", from, to);
    File *fo;
    FileManager *fm = FileManager :: getFileManager();
    uint8_t buffer[1024];

    FILE *fi = fopen(from, "rb");
    if (!fi) {
        return FR_NO_FILE;
    }

    FRESULT fres = fm->fopen(to, FA_WRITE | FA_CREATE_ALWAYS, &fo);
    if (fres == FR_OK) {
        uint32_t tr;
        do {
            tr = fread(buffer, 1, 1024, fi);
            fres = fo->write(buffer, tr, &tr);
            printf("%d\n", tr);
        } while(tr);
        fclose(fi);
        fm->fclose(fo);
    } else {
        printf("File '%s' could not be opened for writing. (%s)\n", to, FileSystem::get_error_string(fres));
    }
    return fres;
}

int ma__in()
{
    FileManager *fm = FileManager :: getFileManager();
    BlockDevice_Emulated iblk("2Part.img", 512);
    FileDevice node(&iblk, (char *)"img", (char *)"Some Image");
    node.attach_disk(512);
    fm->add_root_entry(&node);
    fm->print_directory("/img");
    fm->print_directory("/img/Part0");
    fm->print_directory("/img/Part1");
    fm->print_directory("/img/Part0/GameSamples");
    fm->remove_root_entry(&node);
    return 0;
}


int ma_in()
{
    FileManager *fm = FileManager :: getFileManager();
//    fm->init_ram_disk();

    BlockDevice_Emulated blk("../testfs/fat/exfat.fat", 512);
    Partition fatpart(&blk, 0, 0, 0);
    FileSystemFAT fatfs(&fatpart);
    fatfs.init();
    Node_DirectFS *fatnode = new Node_DirectFS(&fatfs, "img");

    BlockDevice_Emulated iblk("../testfs/test.iso", 2048);
    FileDevice isonode(&iblk, (char *)"iso", (char *)"ISO Image");
    isonode.attach_disk(2048);

    FileSystem *dummy_fs = new FileSystemDummy();
    Node_DirectFS *dummy = new Node_DirectFS(dummy_fs, "ftp");

    fm->add_root_entry(fatnode);
    fm->add_root_entry(dummy);
    fm->add_root_entry(&isonode);

/*
    fm->print_directory("/");
    fm->print_directory("/img");
    fm->print_directory("/img/main.dnp");
    fm->print_directory("/img/main.dnp/utilities");
    fm->print_directory("/iso");
    fm->print_directory("/iso/fat");
    fm->print_directory("/iso/fat/sd256.fat");
    fm->print_directory("/iso/fat/sd256.fat/code23.d64");
*/

    copy_from("/img/main.dnp/utilities/gifvert.prg", "gifvert.prg");
    copy_from("/iso/fat/sd256.fat/code23.d64/turbo ass. v5.5!.prg", "tas.prg");
    copy_to("gifvert.prg", "/tmp/gifvert.prg");
    fm->print_directory("/tmp");

/*
    const char *testfile = "/img/main.dnp/utilities/gideon.seq";
    File *f = 0;
    FRESULT fres = fm->delete_file(testfile);
    printf("Delete result: %s\n", FileSystem::get_error_string(fres));
    fres = fm->fopen(testfile, FA_CREATE_NEW, &f);
    if(f) {
        uint32_t tr;
        f->write("Hello", 5, &tr);
        fm->fclose(f);
    } else {
        printf("Create result: %s\n", FileSystem::get_error_string(fres));
    }
    FileInfo info(128);
    fres = fm->fstat(testfile, info);
    if (fres == FR_OK) {
        info.print_info();
    } else {
        printf("FStat failed. %s\n", FileSystem::get_error_string(fres));
    }
    fm->print_directory("/img/main.dnp/utilities");

    fres = fm->fopen(testfile, FA_READ, &f);
*/

    fm->dump();

    fm->remove_root_entry(dummy);
    fm->remove_root_entry(fatnode);
    fm->remove_root_entry(&isonode);

    delete dummy;
    delete dummy_fs;
    delete fatnode;
    return 0;
}

int main()
{
    FileManager *fm = FileManager :: getFileManager();

    BlockDevice *blk = new BlockDevice_Emulated("/dev/sdb", 512);
    Disk dsk(blk, 512);
    int partition_count = dsk.Init(false);
    printf("%d partitions found.\n", partition_count);
    if (partition_count > 0) {
        Partition *prt = dsk.partition_list;
        printf("Prt = %p\n", prt);
        FileSystem *fs = prt->attach_filesystem();
        printf("FS = %p\n", fs);
        Node_DirectFS *fatnode = new Node_DirectFS(fs, "img");

        fm->add_root_entry(fatnode);

        fm->print_directory("/img");
        fm->print_directory("/img/Games/CSDB/Top200/019. Giana Sisters +9DFH");

        copy_from("/img/Games/CSDB/Top200/019. Giana Sisters +9DFH/274_Great_Giana_Sisters_1987_Rainbow_Arts.d64", "giana.d64");

        fm->dump();

        fm->remove_root_entry(fatnode);

        delete fs;
        delete fatnode;
    }        
    return 0;
}
