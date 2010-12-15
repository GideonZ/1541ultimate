
#include <stdio.h>
#include <sys/stat.h>
#include "blockdev_emul.h"
#include "partition.h"
#include "iso9660.h"
#include "dump_hex.h"

//extern "C" {
    void outbyte(int c)
    {
        putc(c, stdout);
    }
//}

void test_file_system(FileSystem *fs)
{
    BYTE *buffer = new BYTE[3000];
    UINT trans;
    Directory *dir = fs->dir_open(NULL);
    if(!dir) {
        printf("Can't open directory.\n");
        return;
    }
    FileInfo *info = new FileInfo(40);
    FRESULT fres;
    do {
        fres = fs->dir_read(dir, info);
        if(fres == FR_OK) {
            info->print_info();
            printf("--------------\n");
            if(!(info->attrib & AM_DIR)) {
                File *f = fs->file_open(info, 0);
                if(f) {
                    fs->file_read(f, buffer, 3000, &trans);
                    dump_hex_relative(buffer, trans);
                    fs->file_close(f);
                }
            }
            printf("--------------\n");
        }
    } while(fres == FR_OK);
    fs->dir_close(dir);    
    delete info;
    delete buffer;
}

int main(int argc, char **argv)
{
    if(argc < 2) {
        printf("Give filename of ISO file.\n");
        return -1;
    }
    BlockDevice *blk = new BlockDevice_Emulated(argv[1], 2048);
    if(blk->status()) {
        printf("Can't open file.\n");
        delete blk;
        return -2;
    }
    DWORD sec_count;
    DRESULT dres = blk->ioctl(GET_SECTOR_COUNT, &sec_count);
    printf("Sector count: %d.\n", sec_count);
    Partition *prt = new Partition(blk, 0, sec_count, 0);
    FileSystem *fs = new FileSystem_ISO9660(prt);
    bool fs_ok = fs->init();
    if(fs_ok) {
        test_file_system(fs);
    }    
    
    delete prt;
    delete blk;
}
