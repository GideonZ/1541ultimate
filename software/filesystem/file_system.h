#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

#include <string.h>
#include <stdio.h>
#include "partition.h"
#include "file_info.h"

/* File function return code (FRESULT) */
typedef enum {
    FR_OK = 0,          /* 0 */
    FR_DISK_ERR,        /* 1 */
    FR_INT_ERR,         /* 2 */
    FR_NOT_READY,       /* 3 */
    FR_NO_FILE,         /* 4 */
    FR_NO_PATH,         /* 5 */
    FR_INVALID_NAME,    /* 6 */
    FR_DENIED,          /* 7 */
    FR_EXIST,           /* 8 */
    FR_INVALID_OBJECT,  /* 9 */
    FR_WRITE_PROTECTED, /* 10 */
    FR_INVALID_DRIVE,   /* 11 */
    FR_NOT_ENABLED,     /* 12 */
    FR_NO_FILESYSTEM,   /* 13 */
    FR_MKFS_ABORTED,    /* 14 */
    FR_TIMEOUT,         /* 15 */
    FR_NO_MEMORY,		/* 16 */
    FR_DISK_FULL,       /* 17 */
	FR_DIR_NOT_EMPTY    /* 18 */
} FRESULT;

/*--------------------------------------------------------------*/
/* File access control and file status flags (FIL.flag)         */

#define FA_READ             0x01
#define FA_OPEN_EXISTING    0x00
#define FA_WRITE            0x02
#define FA_CREATE_NEW       0x04
#define FA_CREATE_ALWAYS    0x08
#define FA_ANY_WRITE_FLAG   0x0E // the three above orred
#define FA_OPEN_ALWAYS      0x10
#define FA__WRITTEN         0x20
#define FA__DIRTY           0x40
#define FA__ERROR           0x80


class Path;

class File;
class Directory;
class FileSystemFactory;

class FileSystem
{
protected:
    Partition *prt;
public:
    FileSystem(Partition *p);
    virtual ~FileSystem();

    virtual FRESULT get_last_error() { return FR_OK; }
	static  const char *get_error_string(FRESULT res);

    virtual bool    init(void);              // Initialize file system
    virtual FRESULT get_free (uint32_t *e) { *e = 0; return FR_OK; } // Get number of free sectors on the file system
    virtual bool is_writable() { return false; } // by default a file system is not writable, unless we implement it
    virtual FRESULT sync(void) { return FR_OK; } // by default we can't write, and syncing is thus always successful
    
    // functions for reading directories
    virtual Directory *dir_open(FileInfo *); // Opens directory (creates dir object, NULL = root)
    virtual void dir_close(Directory *d);    // Closes (and destructs dir object)
    virtual FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir
    virtual FRESULT dir_create(FileInfo *);  // Creates a directory as specified by finfo
    
    // functions for reading and writing files
    virtual File   *file_open(FileInfo *, uint8_t flags);  // Opens file (creates file object)
    virtual FRESULT file_rename(FileInfo *, char *new_name); // Renames a file
	virtual FRESULT file_delete(FileInfo *); // deletes a file
    virtual void    file_close(File *f);                // Closes file (and destructs file object)
    virtual FRESULT file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred);
    virtual FRESULT file_write(File *f, void *buffer, uint32_t len, uint32_t *transferred);
    virtual FRESULT file_seek(File *f, uint32_t pos);
    virtual FRESULT file_sync(File *f);             // Clean-up cached data
    virtual void    file_print_info(File *f) { } // debug
    
};

#include "factory.h"

typedef FileSystem *(*fileSystemTestFunction_t)(Partition *p);

extern Factory<Partition *, FileSystem *> file_system_factory;


// FATFS is a derivate from this base class. This base class implements the interface

#include "directory.h"
#include "path.h"
#include "file.h"

#endif
