/*
 * embedded_t64.cc
 *
 *  Created on: Jun 14, 2015
 *      Author: Gideon
 */

#include "embedded_t64.h"
#include "globals.h"

// tester instance
FactoryRegistrator<FileInfo *, FileSystemInFile *>
	tester_emb_t64(Globals :: getEmbeddedFileSystemFactory(), FileSystemInFile_T64 :: test_type);


FileSystemInFile_T64 :: FileSystemInFile_T64() {
	fs = 0;
}

FileSystemInFile_T64 :: ~FileSystemInFile_T64() {
	if (fs)
		delete fs;
}

void FileSystemInFile_T64 :: init(File *f)
{
	fs  = new FileSystemT64(f);
}

FileSystemInFile *FileSystemInFile_T64 :: test_type(FileInfo *inf)
{
    if(strcmp(inf->extension, "T64")==0)
        return new FileSystemInFile_T64();
    return NULL;
}
