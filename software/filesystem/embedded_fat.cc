/*
 * embedded_fat.cc
 *
 *  Created on: Jun 14, 2015
 *      Author: Gideon
 */

#include "embedded_fat.h"

// tester instance
FactoryRegistrator<FileInfo *, FileSystemInFile *>
	tester_emb_fat(FileSystemInFile :: getEmbeddedFileSystemFactory(), FileSystemInFile_FAT :: test_type);

FileSystemInFile_FAT :: FileSystemInFile_FAT() {
	fs = 0;
	blk = 0;
	prt = 0;
	fs = 0;
}

FileSystemInFile_FAT :: ~FileSystemInFile_FAT() {
	if (fs)
		delete fs;
	if (prt)
		delete prt;
	if (blk)
		delete blk;
}

void FileSystemInFile_FAT :: init(File *f)
{
	printf("Creating FAT file system in File: %s.\n", f->get_path());
    fs = NULL;
    uint32_t transferred = 0;
    uint8_t secsize[2];
    
    FRESULT fres;
    fres = f->seek(11);
    if (fres != FR_OK) {
        return;
    }
    fres = f->read(secsize, 2, &transferred);
    if (fres != FR_OK) {
        return;
    }
    int sector_size = (int)secsize[0] + (int)(((unsigned int)secsize[1]) << 8);
    if (sector_size & (sector_size - 1) != 0) {
        printf("Invalid sector size %d\n", sector_size);
        return;
    }
    if ((sector_size < 512) || (sector_size > 4096)) {
        printf("Unsupported sector size %d", sector_size);
        return;
    }
    blk = new BlockDevice_File(f, sector_size);
    if(blk->status()) {
        printf("Can't open file.\n");
    } else {
        uint32_t sec_count;
        blk->ioctl(GET_SECTOR_COUNT, &sec_count);
        printf("Sector size %d / count: %d.\n", sector_size, sec_count);
        prt = new Partition(blk, 0, sec_count, 0);
        fs = new FileSystemFAT(prt);
    }
    if(fs) {
        if(!fs->init()) {
            delete fs;
			delete prt;
			delete blk;
            fs = NULL;
        }
    }
}

FileSystemInFile *FileSystemInFile_FAT :: test_type(FileInfo *inf)
{
    if(strcmp(inf->extension, "FAT")==0)
        return new FileSystemInFile_FAT();
    return NULL;
}
