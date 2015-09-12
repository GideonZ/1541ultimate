/*
 * embedded_fat.cc
 *
 *  Created on: Jun 14, 2015
 *      Author: Gideon
 */

#include "embedded_fat.h"
#include "globals.h"

// tester instance
FactoryRegistrator<FileInfo *, FileSystemInFile *>
	tester_emb_fat(Globals :: getEmbeddedFileSystemFactory(), FileSystemInFile_FAT :: test_type);

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

    blk = new BlockDevice_File(f, 512);
    if(blk->status()) {
        printf("Can't open file.\n");
    } else {
        uint32_t sec_count;
        blk->ioctl(GET_SECTOR_COUNT, &sec_count);
        printf("Sector count: %d.\n", sec_count);
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
