/*
 * d64_filesystem.h
 *
 *  Created on: May 9, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_D64_FILESYSTEM_H_
#define FILESYSTEM_D64_FILESYSTEM_H_

#include "file_system.h"
#include "partition.h"

class FileSystemD64;

class DirInD64
{
    int idx;
    uint8_t *visited;  // this should probably be a bit vector

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
	friend class FileSystemD64;

	int start_cluster;
	int current_track;
    int current_sector;
    int offset_in_sector;
    int num_blocks;
    int dir_sect;
    int dir_entry_offset;
    uint8_t *visited;  // this should probably be a bit vector

    FileSystemD64 *fs;

    FRESULT visit(void);
public:
    FileInD64(FileSystemD64 *);
    ~FileInD64() { }

    FRESULT open(FileInfo *info, uint8_t flags);
    FRESULT close(void);
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT seek(uint32_t pos);
};


class FileSystemD64 : public FileSystem
{
    uint8_t sect_buffer[256]; // one sector
    uint8_t bam_buffer[256];
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
    FRESULT get_free (uint32_t*);        // Get number of free sectors on the file system
    FRESULT sync(void);               // Clean-up cached data

    // functions for reading directories
    FRESULT dir_open(const char *path, Directory **, FileInfo *inf = 0); // Opens directory (creates dir object, NULL = root)
    void dir_close(Directory *d);    // Closes (and destructs dir object)
    FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir

    // functions for reading and writing files
    FRESULT file_open(const char *path, Directory *dir, const char *filename, uint8_t flags, File **file);  // Opens file (creates file object)
    void    file_close(File *f);                // Closes file (and destructs file object)
    FRESULT file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_write(File *f, const void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_seek(File *f, uint32_t pos);

    uint32_t get_file_size(File *f) {
        FileInD64 *handle = (FileInD64 *)f->handle;
        return (uint32_t)(254 * handle->num_blocks);
    }

    uint32_t get_inode(File *f) {
        FileInD64 *handle = (FileInD64 *)f->handle;
        return (uint32_t)(handle->start_cluster);
    }

    friend class DirInD64;
    friend class FileInD64;
};


#endif /* FILESYSTEM_D64_FILESYSTEM_H_ */
