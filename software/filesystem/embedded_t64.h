/*
 * embedded_t64.h
 *
 *  Created on: Jun 14, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_EMBEDDED_T64_H_
#define FILESYSTEM_EMBEDDED_T64_H_

#include "embedded_fs.h"
#include "t64_filesystem.h"

class FileSystemInFile_T64: public FileSystemInFile {
	FileSystemT64 *fs;
public:
	FileSystemInFile_T64();
	virtual ~FileSystemInFile_T64();

	void init(File *f);
	FileSystem *getFileSystem() { return fs; }

	static FileSystemInFile *test_type(FileInfo *inf);
};

#endif /* FILESYSTEM_EMBEDDED_T64_H_ */
