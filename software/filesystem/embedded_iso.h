/*
 * embedded_iso.h
 *
 *  Created on: Jun 14, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_EMBEDDED_ISO_H_
#define FILESYSTEM_EMBEDDED_ISO_H_

#include "embedded_fs.h"
#include "iso9660.h"
#include "blockdev_file.h"
#include "partition.h"

class FileSystemInFile_ISO: public FileSystemInFile {
	FileSystem_ISO9660 *fs;
	BlockDevice_File *blk;
    Partition *prt;
public:
	FileSystemInFile_ISO();
	virtual ~FileSystemInFile_ISO();

	void init(File *f);
	FileSystem *getFileSystem() { return fs; }

	static FileSystemInFile *test_type(FileInfo *inf);
};

#endif /* FILESYSTEM_EMBEDDED_ISO_H_ */
