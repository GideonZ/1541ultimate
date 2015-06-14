/*
 * filesystem_in_file.h
 *
 *  Created on: Jun 10, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_EMBEDDED_FS_H_
#define FILESYSTEM_EMBEDDED_FS_H_

class File;
class FileSystem;

class FileSystemInFile {
public:
	FileSystemInFile() {  }
	virtual ~FileSystemInFile() { }

	virtual void init(File *f) { }
	virtual FileSystem *getFileSystem() { return 0; }
};

#endif /* FILESYSTEM_EMBEDDED_FS_H_ */
