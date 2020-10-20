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

class CachedBlock
{
public:
    uint8_t *data;
    bool dirty;
    const int size;
    const uint8_t track;
    const uint8_t sector;

    CachedBlock(int sz, uint8_t track, uint8_t sector) : track(track), sector(sector), size(sz)
    {
        data = new uint8_t[sz];
        memset(data, 0, sz);
        dirty = false;
    }

    ~CachedBlock() {
        delete[] data;
    }
};

struct DirEntryCBM
{
    uint8_t dummy[2];
    uint8_t std_fileType;
    uint8_t data_track;
    uint8_t data_sector;
    uint8_t name[16];
    uint8_t aux_track;
    uint8_t aux_sector;
    union {
        uint8_t record_size;
        uint8_t geos_structure;
    };
    uint8_t geos_filetype;
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t size_low;
    uint8_t size_high;
};

class FileSystemCBM;

class DirInCBM : public Directory
{
    int idx;
    int curr_t, curr_s;
    int next_t, next_s;
    int header_track, header_sector;
    int start_track, start_sector;

    uint8_t *visited;  // this should probably be a bit vector
    DirEntryCBM *get_pointer(void);
    FileSystemCBM *fs;

    FRESULT open(void);
    FRESULT close(void);
    FRESULT create(const char *filename, bool dir);
public:
    DirInCBM(FileSystemCBM *fs, FileInfo *info = NULL);
    ~DirInCBM();

    FRESULT get_entry(FileInfo &out);
    friend class FileSystemCBM;
};

class SideSectors;
class SideSectorCluster;

class FileInCBM
{
    enum {
        ST_HEADER,
        ST_LINEAR,
        ST_CVT,
    } state;

    struct {
        int pos;
        int size;
        uint8_t *data;
    } header;

    friend class FileSystemCBM;
	DirEntryCBM dir_entry;
	int start_cluster;
	int current_track;
    int current_sector;
    int offset_in_sector;
    int num_blocks;
    int file_size;
    int dir_sect;
    int dir_entry_offset;
    int dir_entry_modified;
    bool isRel;
    uint8_t *visited;  // this should probably be a bit vector

    FileSystemCBM *fs;
    SideSectors *side;

    FRESULT read_header(uint8_t *dst, int len, uint32_t& transferred);
    FRESULT read_linear(uint8_t *dst, int len, uint32_t& transferred);
    FRESULT write_header(uint8_t *src, int len, uint32_t& transferred);
    FRESULT write_linear(uint8_t *src, int len, uint32_t& transferred);
    FRESULT visit(void);
    FRESULT followChain(int track, int sector, int& noSectors, int& bytesLastSector);
public:
    FileInCBM(FileSystemCBM *, DirEntryCBM *, int dir_t, int dir_s, int dir_idx);
    ~FileInCBM();

    FRESULT open(uint8_t flags);
    FRESULT close(void);
    FRESULT read(void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT write(const void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT seek(uint32_t pos);

#if SS_DEBUG
    void dumpSideSectors();
#endif
};

class FileSystemCBM : public FileSystem
{
	// Disk layout parameters
	const int  *layout;
	int root_track, root_sector;
	int dir_track, dir_sector;
	int volume_name_offset;

	uint8_t sect_buffer[256]; // one sector
    uint8_t root_buffer[256];
    bool root_valid;
    bool root_dirty;
    bool writable;
    int  current_sector;
    int  dirty;
    int  num_sectors;

    void    set_volume_name(const char *name, uint8_t *bam_name, const char *dos);
    bool    modify_allocation_bit(uint8_t *fr, uint8_t *bits, int sector, bool alloc);
    FRESULT move_window(int);
    FRESULT find_file(const char *filename, DirInCBM *dirent, FileInfo *info);

    void    get_volume_name(uint8_t *sector, char *buffer, int buf_len);
    int     get_abs_sector(int track, int sector);
    bool    get_track_sector(int abs, int &track, int &sector);
    FRESULT deallocate_chain(uint8_t track, uint8_t sector, uint8_t *visited);

    //  base class functions, to be filled with dummies
    virtual bool allocate_sector_on_track(int track, int &sector) { return false; }
    virtual bool set_sector_allocation(int track, int sector, bool alloc)  { return false; };
    virtual bool get_next_free_sector(int &track, int &sector);

public:
    FileSystemCBM(Partition *p, bool writable, const int *lay);
    virtual ~FileSystemCBM();

    static  bool check(Partition *p); // check if file system is present on this partition
    virtual bool init(void);               // Initialize
    bool    is_writable();

    // Create initial structures of empty disk
    virtual FRESULT format(const char *name) { return FR_NO_FILESYSTEM; }
    // Get number of free sectors on the file system
    virtual FRESULT get_free (uint32_t*) { return FR_NO_FILESYSTEM; }
    // Clean-up cached data
    virtual FRESULT sync(void);

    // functions for reading directories
    FRESULT dir_open(const char *path, Directory **, FileInfo *relativeDir = 0); // Opens directory (creates dir object)
    FRESULT dir_create(const char *path);

    // functions for reading and writing files
    FRESULT file_open(const char *filename, uint8_t flags, File **, FileInfo *relativeDir = 0);  // Opens file (creates file object)
    void    file_close(File *f);                // Closes file (and destructs file object)
    FRESULT file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_write(File *f, const void *buffer, uint32_t len, uint32_t *transferred);
    FRESULT file_seek(File *f, uint32_t pos);

    FRESULT file_rename(const char *old_name, const char *new_name);  // Renames a file
    FRESULT file_delete(const char *path); // deletes a file

    uint32_t get_file_size(File *f) {
        FileInCBM *handle = (FileInCBM *)f->handle;
        return handle->file_size;
    }

    uint32_t get_inode(File *f) {
        FileInCBM *handle = (FileInCBM *)f->handle;
        return (uint32_t)(handle->start_cluster);
    }

    friend class DirInCBM;
    friend class FileInCBM;
    friend class FileSystemD64;
    friend class FileSystemD71;
    friend class FileSystemD81;
    friend class FileSystemDNP;
    friend class SideSectors;
    friend class SideSectorCluster;
};

static const int layout_d64[] = { 17, 21, 7, 19, 6, 18, -1, 17 };
static const int layout_d71[] = { 17, 21, 7, 19, 6, 18, 5, 17,
                                  17, 21, 7, 19, 6, 18, -1, 17 };
static const int layout_d81[] = { -1, 40 };
static const int layout_dnp[] = { -1, 256 };

class FileSystemD64 : public FileSystemCBM
{
	bool allocate_sector_on_track(int track, int &sector);
    bool set_sector_allocation(int track, int sector, bool alloc);
public:
    FileSystemD64(Partition *p, bool writable) : FileSystemCBM(p, writable, layout_d64) {
        init();
    }

	~FileSystemD64() { }

    bool init(void);
    FRESULT format(const char *name);
    FRESULT get_free (uint32_t*);
};

class FileSystemD71 : public FileSystemCBM
{
	uint8_t bam2_buffer[256];
	bool bam2_dirty;
	bool bam2_valid;

	// root and dir sectors are the same
	// volume name is at the same location
    bool allocate_sector_on_track(int track, int &sector);
	bool set_sector_allocation(int track, int sector, bool alloc);
    bool get_next_free_sector(int &track, int &sector);
public:
    FileSystemD71(Partition *p, bool writable) : FileSystemCBM(p, writable, layout_d71)
	{
        init();
    	bam2_dirty = false;
    	if(p->read(bam2_buffer, get_abs_sector(53, 0), 1) == RES_OK) {
            bam2_valid = true;
        }
	}

	~FileSystemD71() { }

	bool init(void);
    FRESULT format(const char *name);
    FRESULT get_free (uint32_t*);

    FRESULT sync(void)
    {
        FRESULT fres = FileSystemCBM :: sync();
        if (fres == FR_OK) {
			if (bam2_dirty) {
				DRESULT res = prt->write(bam2_buffer, get_abs_sector(53, 0), 1);
				if(res != RES_OK) {
					return FR_DISK_ERR;
				}
				bam2_dirty = false;
			}
        }
    	return fres;
    }
};

class FileSystemD81 : public FileSystemCBM
{
	uint8_t bam_buffer[512]; // two sectors!
	bool bam_dirty;
	bool bam_valid;

    bool allocate_sector_on_track(int track, int &sector);
	bool set_sector_allocation(int track, int sector, bool alloc);
public:
    FileSystemD81(Partition *p, bool writable) : FileSystemCBM(p, writable, layout_d81)
	{
        init();
	}

    ~FileSystemD81() { }

    bool init(void);
    FRESULT format(const char *name);
    FRESULT get_free (uint32_t*);

    FRESULT sync(void)
    {
        FRESULT fres = FileSystemCBM :: sync();
        if (fres == FR_OK) {
			if (bam_dirty) {
				DRESULT res = prt->write(bam_buffer, get_abs_sector(40, 1), 2);
				if(res != RES_OK) {
					return FR_DISK_ERR;
				}
				bam_dirty = false;
			}
        }
    	return fres;
    }
};

class FileSystemDNP : public FileSystemCBM
{
    uint8_t bam_buffer[8192]; // 32 sectors!!
    uint32_t bam_dirty;
    bool bam_valid;

    bool get_next_free_sector(int &track, int &sector);
    bool set_sector_allocation(int track, int sector, bool alloc);
public:
    FileSystemDNP(Partition *p, bool writable) : FileSystemCBM(p, writable, layout_dnp) {
        init();
    }

    ~FileSystemDNP() { }

    bool init(void);
    FRESULT format(const char *name);
    FRESULT get_free (uint32_t*);
    FRESULT sync(void);
};

#if 0
int section;
uint8_t vlir[256];
uint8_t tmpBuffer[256];
bool isVlir;
FRESULT openCVT(FileInfo *info, uint8_t flags,int,int,int);
#endif

#endif /* FILESYSTEM_D64_FILESYSTEM_H_ */
