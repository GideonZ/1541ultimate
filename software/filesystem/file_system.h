#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

#include <string.h>
#include <stdio.h>
#include "partition.h"
#include "file_info.h"
#include "fs_errors_flags.h"

class FileSystem;
class Path;

typedef enum {
	e_DirNotFound,
	e_EntryNotFound,
	e_EntryFound,
	e_TerminatedOnFile,
	e_TerminatedOnMount
} PathStatus_t;

class PathInfo;
class File;
class Directory;

class FileSystem
{
protected:
    Partition *prt;
public:
    FileSystem(Partition *p);
    virtual ~FileSystem();

	static  const char *get_error_string(FRESULT res);

	virtual PathStatus_t walk_path(Path *path, const char *fn, PathInfo& pathInfo);

	virtual bool    init(void);              // Initialize file system
    virtual FRESULT get_free (uint32_t *e) { *e = 0; return FR_OK; } // Get number of free sectors on the file system
    virtual bool is_writable() { return false; } // by default a file system is not writable, unless we implement it
    virtual FRESULT sync(void) { return FR_OK; } // by default we can't write, and syncing is thus always successful
    
    // functions for reading directories
    virtual FRESULT dir_open(const char *path, Directory **); // Opens directory (creates dir object, NULL = root)
    virtual void    dir_close(Directory *d);    // Closes (and destructs dir object)
    virtual FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir
    virtual FRESULT dir_create(const char *path);  // Creates a directory
    
    // functions for reading and writing files
    virtual FRESULT file_stat(Path *path, const char *fn, FileInfo *inf);
    virtual FRESULT file_open(const char *path, uint8_t flags, File **);  // Opens file (creates file object)
    virtual FRESULT file_rename(const char *old_name, const char *new_name); // Renames a file
	virtual FRESULT file_delete(const char *path); // deletes a file
    virtual void    file_close(File *f);
    virtual FRESULT file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred);
    virtual FRESULT file_write(File *f, void *buffer, uint32_t len, uint32_t *transferred);
    virtual FRESULT file_seek(File *f, uint32_t pos);
    virtual FRESULT file_sync(File *f);             // Clean-up cached data
    virtual void    file_print_info(File *f) { } // debug
};

#include "factory.h"

typedef FileSystem *(*fileSystemTestFunction_t)(Partition *p);

extern Factory<Partition *, FileSystem *> file_system_factory;

#include "directory.h"
#include "path.h"
#include "file.h"

class PathInfo {
public:
	Path pathFromRootOfFileSystem;
	Path remainingPath;
	FileSystem *fileSystem;
	FileInfo fileInfo;

	PathInfo() {
		fileSystem = 0;
	}
};

#endif
