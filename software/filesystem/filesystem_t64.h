/*
 * t64_filesystem.h
 *
 *  Created on: May 25, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_T64_FILESYSTEM_H_
#define FILESYSTEM_T64_FILESYSTEM_H_

#include "blockdev_file.h"
#include "partition.h"
#include "file_system.h"

class FileSystemT64 : public FileSystem
{
	File *t64_file;

	void    openT64File();
public:
    FileSystemT64(File *file);
    ~FileSystemT64();

    File *getFile() { return t64_file; }

    FRESULT get_free (uint32_t*, uint32_t*);        // Get number of free sectors on the file system

    // functions for reading directories
    FRESULT dir_open(const char *path, Directory **); // Opens directory (creates dir object)

    // functions for reading and writing files
    FRESULT file_open(const char *filename, uint8_t flags, File **);  // Opens file (creates file object)
    FRESULT sync();

    friend class FileInT64;
};

class DirectoryT64 : public Directory
{
	FileSystemT64 *fs;
	uint32_t idx;
	File *t64_file;
	int max, used;
	uint16_t strt, stop;
public:
	DirectoryT64(FileSystemT64 *fs) {
		this->fs = fs;
		t64_file = fs->getFile();
		idx = 0;
		max = 0;
		used = 0;
		strt = 0;
		stop = 0;
	}
	~DirectoryT64() { }

    FRESULT get_entry(FileInfo &out);
};

class FileInT64 : public File
{
    FileSystemT64 *fs;
    int file_offset;
    int offset;
    int length;
    uint16_t start_addr;
    FRESULT open(FileInfo *info, uint8_t flags);

public:
    FileInT64(FileSystemT64 *);
    ~FileInT64();

    FRESULT close(void);
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT seek(uint32_t pos);
    uint32_t get_size(void);
    uint32_t get_inode(void);

    friend class FileSystemT64;
};


#endif /* FILESYSTEM_T64_FILESYSTEM_H_ */
