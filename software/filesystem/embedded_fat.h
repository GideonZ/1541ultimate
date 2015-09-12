/*
 * embedded_fat.h
 *
 *  Created on: Jun 14, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_EMBEDDED_FAT_H_
#define FILESYSTEM_EMBEDDED_FAT_H_

#include "embedded_fs.h"
#include "filesystem_fat.h"
#include "blockdev_file.h"
#include "partition.h"

class FileSystemInFile_FAT: public FileSystemInFile {
	FileSystemFAT *fs;
	BlockDevice_File *blk;
    Partition *prt;
public:
	FileSystemInFile_FAT();
	virtual ~FileSystemInFile_FAT();

	void init(File *f);
	FileSystem *getFileSystem() { return fs; }

	static FileSystemInFile *test_type(FileInfo *inf);
};

#endif /* FILESYSTEM_EMBEDDED_FAT_H_ */
