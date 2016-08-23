/*
 * filesystem_in_file.h
 *
 *  Created on: Jun 10, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_EMBEDDED_FS_H_
#define FILESYSTEM_EMBEDDED_FS_H_

#include "factory.h"

class File;
class FileSystem;
class FileInfo;

class FileSystemInFile {
public:
	static Factory<FileInfo *, FileSystemInFile *>* getEmbeddedFileSystemFactory() {
		static Factory<FileInfo *, FileSystemInFile *> embedded_fs_factory;
		return &embedded_fs_factory;
	}

	FileSystemInFile() {  }
	virtual ~FileSystemInFile() { }

	virtual void init(File *f) { }
	virtual FileSystem *getFileSystem() { return 0; }
};

#endif /* FILESYSTEM_EMBEDDED_FS_H_ */
