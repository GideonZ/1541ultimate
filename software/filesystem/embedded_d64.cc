/*
 * embedded_d64.cc
 *
 *  Created on: Jun 10, 2015
 *      Author: Gideon
 */

#include "embedded_d64.h"
#include "filesystem_d64.h"

// tester instance
FactoryRegistrator<FileInfo *, FileSystemInFile *>
	tester_emb_d64(FileSystemInFile :: getEmbeddedFileSystemFactory(), FileSystemInFile_D64 :: test_type);

FileSystemInFile_D64::FileSystemInFile_D64(int mode) {
	this->mode = mode;
	file = 0;
	blk = 0;
	prt = 0;
	fs = 0;
}

FileSystemInFile_D64::~FileSystemInFile_D64() {
	if (fs)
		delete fs;
	if (prt)
		delete prt;
	if (blk)
		delete blk;
}

void FileSystemInFile_D64 :: init(File *f)
{
	this->file = f;

	printf("Creating D64 file system in File: %s.\n", f->get_path());

	blk = new BlockDevice_File(file, 256);
	if(blk)
		prt = new Partition(blk, 0, f->get_size() >> 8, 0);
	if(prt)
		fs  = new FileSystemD64(prt);
}


FileSystemInFile *FileSystemInFile_D64 :: test_type(FileInfo *inf)
{
    if(strcmp(inf->extension, "D64")==0)
        return new FileSystemInFile_D64(0);
    if(strcmp(inf->extension, "D71")==0)
        return new FileSystemInFile_D64(1);
    if(strcmp(inf->extension, "D81")==0)
        return new FileSystemInFile_D64(2);
    return NULL;
}
