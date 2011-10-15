#ifndef FILETYPE_D64_H
#define FILETYPE_D64_H

#include "file_direntry.h"
#include "blockdev_file.h"
#include "partition.h"
#include "file_system.h"

class FileTypeD64 : public FileDirEntry
{
    BlockDevice_File *blk;
    Partition *prt;
    FileSystem *fs;
public:
    FileTypeD64(FileTypeFactory &fac);
    FileTypeD64(PathObject *par, FileInfo *fi);
    virtual ~FileTypeD64();

    int   fetch_children(void);
	int   get_header_lines(void) { return 1; }
    int   fetch_context_items(IndexedList<PathObject *> &list);
    FileDirEntry *test_type(PathObject *obj);

    void  execute(int selection);
};

class FileSystemD64 : public FileSystem
{
    BYTE sect_buffer[256]; // one sector
    BYTE bam_buffer[256];
    bool bam_valid;
    bool bam_dirty;
    int  image_mode;
    int  current_sector;
    int  dirty;

    int  num_sectors;

    FRESULT move_window(int);
    int  get_root_sector(void);
    int  get_abs_sector(int track, int sector);
    bool get_track_sector(int abs, int &track, int &sector);
    bool allocate_sector_on_track(int track, int &sector);
    bool get_next_free_sector(int &track, int &sector);
public:
    FileSystemD64(Partition *p);
    ~FileSystemD64();
    
    static  bool check(Partition *p); // check if file system is present on this partition
    bool    init(void);               // Initialize file system
    FRESULT get_free (DWORD*);        // Get number of free sectors on the file system
    FRESULT sync(void);               // Clean-up cached data

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
    
    friend class DirInD64;
    friend class FileInD64;
};

class DirInD64
{
    int idx;
    BYTE *visited;  // this should probably be a bit vector

    FileSystemD64 *fs;
public:
    DirInD64(FileSystemD64 *);
    ~DirInD64() { }

    FRESULT open(FileInfo *info);
    FRESULT close(void);
    FRESULT read(FileInfo *f);
};

class FileInD64
{
    int current_track;
    int current_sector;
    int offset_in_sector;
    int num_blocks;
    int dir_sect;
    int dir_entry_offset;
    BYTE *visited;  // this should probably be a bit vector
    
    FileSystemD64 *fs;

    FRESULT visit(void);
public:
    FileInD64(FileSystemD64 *);
    ~FileInD64() { }

    FRESULT open(FileInfo *info, BYTE flags);
    FRESULT close(void);
    FRESULT read(void *buffer, DWORD len, UINT *transferred);
    FRESULT write(void *buffer, DWORD len, UINT *transferred);
    FRESULT seek(DWORD pos);
};


#endif
