/*
 * embedded_d64.h
 *
 *  Created on: Jun 10, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_EMBEDDED_D64_H_
#define FILESYSTEM_EMBEDDED_D64_H_

#include "embedded_fs.h"
#include "filemanager.h"
#include "blockdev_file.h"
#include "partition.h"
#include "file_system.h"

class FileSystemInFile_D64: public FileSystemInFile {
	File *file;
	BlockDevice_File *blk;
    Partition *prt;
    FileSystem *fs;
    int mode;
public:
	FileSystemInFile_D64(int mode);
	virtual ~FileSystemInFile_D64();

	void init(File *f);
	FileSystem *getFileSystem() { return fs; }

	static FileSystemInFile *test_type(CachedTreeNode *obj);
};

#endif /* FILESYSTEM_EMBEDDED_D64_H_ */
