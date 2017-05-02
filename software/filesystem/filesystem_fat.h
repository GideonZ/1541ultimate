/*
 * fat_filesystem.h
 *
 *  Created on: Aug 23, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_FAT_FILESYSTEM_H_
#define FILESYSTEM_FAT_FILESYSTEM_H_

#include "file_system.h"
#include "ff2.h"

class FileSystemFAT : public FileSystem
{
	FATFS fatfs;
	void copy_info(FILINFO *fi, FileInfo *inf);
public:
    FileSystemFAT(Partition *p);
    virtual ~FileSystemFAT();

    static FileSystem *test (Partition *prt);

    bool    init(void);              // Initialize file system
    FRESULT get_free (uint32_t *e);  // Get number of free sectors on the file system
    bool is_writable();				 // by default a file system is not writable, unless we implement it
    FRESULT sync(void); 			 // by default we can't write, and syncing is thus always successful

    // functions for reading directories
    FRESULT dir_open(const char *path, Directory **, FileInfo *inf); // Opens directory (creates dir object, NULL = root)
    void    dir_close(Directory *d);    // Closes (and destructs dir object)
    FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir
    FRESULT dir_create(const char *path);  // Creates a directory

    // functions for reading and writing files
    FRESULT file_open(const char *path, Directory *, const char *filename, uint8_t flags, File **);
    FRESULT file_open(const char *path, uint8_t flags, File **);  // Opens file (creates file object)
    FRESULT file_rename(const char *old_name, const char *new_name); // Renames a file
	FRESULT file_delete(const char *name); // deletes a file

	void    file_close(File *f);                // Closes file (and destructs file object)
    FRESULT file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_write(File *f, const void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_seek(File *f, uint32_t pos);
    FRESULT file_sync(File *f);             // Clean-up cached data

    uint32_t get_file_size(File *f);
    uint32_t get_inode(File *f);
    bool     needs_sorting() { return true; }
};



#endif /* FILESYSTEM_FAT_FILESYSTEM_H_ */
