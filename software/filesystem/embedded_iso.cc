/*
 * embedded_iso.cc
 *
 *  Created on: Jun 14, 2015
 *      Author: Gideon
 */

#include "embedded_iso.h"
#include "globals.h"

// tester instance
FactoryRegistrator<CachedTreeNode *, FileSystemInFile *>
	tester_emb_iso(Globals :: getEmbeddedFileSystemFactory(), FileSystemInFile_ISO :: test_type);

FileSystemInFile_ISO :: FileSystemInFile_ISO() {
	fs = 0;
	blk = 0;
	prt = 0;
	fs = 0;
}

FileSystemInFile_ISO :: ~FileSystemInFile_ISO() {
	if (fs)
		delete fs;
	if (prt)
		delete prt;
	if (blk)
		delete blk;
}

void FileSystemInFile_ISO :: init(File *f)
{
	FileInfo *fi = f->getFileInfo();
	printf("Creating ISO file system in File: %s.\n", fi->lfname);

    blk = new BlockDevice_File(f, 2048);
    if(blk->status()) {
        printf("Can't open file.\n");
    } else {
        uint32_t sec_count;
        blk->ioctl(GET_SECTOR_COUNT, &sec_count);
        printf("Sector count: %d.\n", sec_count);
        prt = new Partition(blk, 0, sec_count, 0);
        fs = new FileSystem_ISO9660(prt);
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

FileSystemInFile *FileSystemInFile_ISO :: test_type(CachedTreeNode *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "ISO")==0)
        return new FileSystemInFile_ISO();
    return NULL;
}
