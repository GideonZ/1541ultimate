#include "disk.h"
#include "fat.h"
#include "dir.h"
#include "fatfile.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    BlockDevice blk("sd256");
    Disk dsk(&blk, 512);
    int res = dsk.Init();

    if(res < 1) {
        printf("Disk Init result: %d\n", res);
        return res;
    }    

    Partition *prt;
    prt = dsk.partition_list; // get first partition
    
    FATFS fs(prt);
    if(fs.check_fs(0)) {
        printf("Did not find FAT file system.\n");
        return -1;
    }

    fs.init_fs();
    fs.print_info();

    DIR dir(&fs);

    FRESULT fres = dir.open(argv[1]);
    if(fres != FR_OK) {
        printf("Opening DIR was not successful (%d).\n", fres);
        return -2;
    }
    
    dir.print_info();
    
    FILINFO info(64);

    while(fres == FR_OK) {
        fres = dir.readentry(&info);
        if(fres == FR_OK) {
            
            char *str;

            if(info.lfname[0])
                str = info.lfname;
            else
                str = info.fname;
                
            printf("%-30s Size: %7d\n", str, info.fsize);
//            printf("%s\n", info.fname);
        }
    }        

    BYTE buffer[32];
    UINT bytes_read;
    UINT bytes_written;
/*
    FIL *file = new FIL(&fs);
    fres = file->open(argv[2], FA_READ);
    printf("File open Result: %d\n", fres);
    fres = file->read(buffer, 32, &bytes_read);
    printf("Bytes read: %d. Result: %d\n", bytes_read, fres);
    delete file;
*/
    if(argc > 2) {
        printf("\nNow I am going to try opening a file.. (%s)\n", argv[2]);
        FIL file(&fs, argv[2], FA_READ);
        fres = file.read(buffer, 32, &bytes_read);
        printf("Bytes read: %d. Result: %d\n", bytes_read, fres);
        
        for(int i=0;i<32;i++) {
            printf("%02x ", buffer[i]);
        } printf("\n");
        file.close();
    }
    
    if(argc > 3) {
        for(int i=0;i<32;i++) {
            buffer[i] = 'A' + i;
        }
        printf("Going to create file %s.\n", argv[3]);
        FIL file(&fs);
        fres = file.open(argv[3], FA_CREATE_ALWAYS | FA_WRITE);
        if(fres == FR_OK) {
            printf("Writing data into file now.\n");
            file.write(buffer, 32, &bytes_written);
            printf("Bytes written: %d\n", bytes_written);
            file.close();
        }
    }
/*
    printf("\nNow going to create some files..\n");
    char myname[64];
    int p = 32;
    for(int i=1;i<45;i++) {
        for(int j=0;j<i;j++) {
            myname[j] = 'A' + (p % 26);
            p = p*7 + 29;
            p &= 0xFFFF;
        }
        myname[i] = '\0';
        fres = file.open(myname, FA_CREATE_NEW | FA_CREATE_ALWAYS);
        printf("Creating file '%s' result = %d. ", myname, fres);
        if(fres == FR_OK) {
            file.fprintf("This is file # %d\n Filename = %s\n", i, myname);
//            file.write(buffer, 32, &bytes_written);
            printf(" written: %d", bytes_written);
            file.close();
        }
        printf("\n");
    }
*/
    DWORD free;
    fres = fs.getfree(&free);
    printf("GetFree returned %d (fres=%d)\n", free, fres);
    
    return 0;
}

