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
	int max, used;
	uint16_t strt, stop;

	void    openT64File();
public:
    FileSystemT64(File *file);
    ~FileSystemT64();

    FRESULT get_free (uint32_t*);        // Get number of free sectors on the file system

    // functions for reading directories
    FRESULT dir_open(const char *path, Directory **, FileInfo *relativeDir = 0); // Opens directory (creates dir object)
    void    dir_close(Directory *d);    // Closes (and destructs dir object)
    FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir

    // functions for reading and writing files
    FRESULT file_open(const char *filename, uint8_t flags, File **, FileInfo *relativeDir = 0);  // Opens file (creates file object)
    void    file_close(File *f);                // Closes file (and destructs file object)
    FRESULT file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_write(File *f, const void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_seek(File *f, uint32_t pos);
    FRESULT sync();

    friend class FileInT64;
};

class FileInT64
{
    FileSystemT64 *fs;
    int file_offset;
    int offset;
    int length;
    uint16_t start_addr;
public:
    FileInT64(FileSystemT64 *);
    ~FileInT64() { }

    FRESULT open(FileInfo *info, uint8_t flags);
    FRESULT close(void);
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT seek(uint32_t pos);
};


#endif /* FILESYSTEM_T64_FILESYSTEM_H_ */
