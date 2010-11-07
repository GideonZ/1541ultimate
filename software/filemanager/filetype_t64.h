#ifndef FILETYPE_T64_H
#define FILETYPE_T64_H

#include "file_direntry.h"
#include "blockdev_file.h"
#include "partition.h"
#include "file_system.h"

class FileSystemT64 : public FileSystem
{
	int max, used;
	WORD strt, stop;
public:
	File *t64_file;
	
    FileSystemT64(Partition *p);
    ~FileSystemT64();
    
    FRESULT get_free (DWORD*);        // Get number of free sectors on the file system

    // functions for reading directories
    Directory *dir_open(FileInfo *); // Opens directory (creates dir object, NULL = root)
    void dir_close(Directory *d);    // Closes (and destructs dir object)
    FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir
    
    // functions for reading and writing files
    File   *file_open(FileInfo *, BYTE flags);  // Opens file (creates file object)
    void    file_close(File *f);                // Closes file (and destructs file object)
    FRESULT file_read(File *f, void *buffer, DWORD len, UINT *transferred);
    FRESULT file_write(File *f, void *buffer, DWORD len, UINT *transferred);
    FRESULT file_seek(File *f, DWORD pos);
    
    friend class FileInT64;
};

class FileTypeT64 : public FileDirEntry
{
	File *file;
    FileSystemT64 *fs;
public:
    FileTypeT64(FileTypeFactory &fac);
    FileTypeT64(PathObject *par, FileInfo *fi);
    ~FileTypeT64();

    bool  is_writable(void) { return false; }
    int   fetch_children(void);
	int   get_header_lines(void) { return 1; }
    int   fetch_context_items(IndexedList<PathObject *> &list);
    FileDirEntry *test_type(PathObject *obj);
};

class FileInT64
{
    FileSystemT64 *fs;
    int file_offset;
    int offset;
    int length;
    WORD start_addr;
public:
    FileInT64(FileSystemT64 *);
    ~FileInT64() { }

    FRESULT open(FileInfo *info, BYTE flags);
    FRESULT close(void);
    FRESULT read(void *buffer, DWORD len, UINT *transferred);
    FRESULT write(void *buffer, DWORD len, UINT *transferred);
    FRESULT seek(DWORD pos);
};


#endif
