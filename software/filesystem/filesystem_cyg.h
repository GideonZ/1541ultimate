#ifndef CYGWIN_FS_H
#define CYGWIN_FS_H

#include "file_system.h"
#include "partition.h"

class FileSystemCygwin : public FileSystem
{
public:
	FileSystemCygwin();
    ~FileSystemCygwin();

    bool    init(void);               // Initialize file system

    // Status
    bool is_writable() { return true; }

    // functions for reading directories
    Directory *dir_open(FileInfo *); // Opens directory (creates dir object, NULL = root)
    void dir_close(Directory *d);    // Closes (and destructs dir object)
    FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir

    // functions for reading and writing files
    File   *file_open(FileInfo *, uint8_t flags);  // Opens file (creates file object)
    void    file_close(File *f);                // Closes file (and destructs file object)
    FRESULT file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_write(File *f, void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_seek(File *f, uint32_t pos);

/*
    friend class DirInCygwin;
    friend class FileInCygwin;
*/
};

/*
class DirInCygwin
{
    FileSystemCygwin *fs;
public:
    DirInCygwin(FileSystemCygwin *);
    ~DirInCygwin() { }

    FRESULT open(FileInfo *info);
    FRESULT close(void);
    FRESULT read(FileInfo *f);
};

class FileInCygwin
{
	FileSystemCygwin *fs;

    FRESULT visit(void);
public:
    FileInCygwin(FileSystemCygwin *);
    ~FileInCygwin() { }

    FRESULT open(FileInfo *info, uint8_t flags);
    FRESULT close(void);
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT write(void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT seek(uint32_t pos);
};
*/

#endif
