#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

#include "partition.h"
#include <string.h>
extern "C" {
    #include "small_printf.h"
}

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

/* File attribute bits for directory entry */

#define AM_RDO   0x01    /* Read only */
#define AM_HID   0x02    /* Hidden */
#define AM_SYS   0x04    /* System */
#define AM_VOL   0x08    /* Volume label */
#define AM_LFN   0x0F    /* LFN entry */
#define AM_DIR   0x10    /* Directory */
#define AM_ARC   0x20    /* Archive */
#define AM_MASK  0x3F    /* Mask of defined bits */

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
class FileSystem;

class FileInfo
{
public:
	FileSystem *fs;  /* Reference to file system, to uniquely identify file */
    DWORD   cluster; /* Start cluster, easy for open! */
    DWORD	size;	 /* File size */
	WORD	date;	 /* Last modified date */
	WORD	time;	 /* Last modified time */
	DWORD   dir_clust;  /* Start of directory, needed to reopen dir */
    WORD    dir_index;  /* Entry of the directory we have our directory item. */
//   DWORD   dir_sector; /* Actual sector in which the directory item is located */
//    WORD    dir_offset; /* Offset within sector where dir entry is located */
//    WORD    lfn_index;  /* dirty hack to easily find the first entry of the name */
                        /* should NOT be in file info... should be handled within the file system itself!! */
    WORD    lfsize;
    char   *lfname;
	BYTE	attrib;	 /* Attribute */
    char    extension[4];
    
	FileInfo(int namesize)
	{
		fs = NULL;
		cluster = 0;
		size = 0;
		date = 0;
		time = 0;
		dir_clust = 0;
//		dir_sector = 2;
//		dir_offset = 0;
//		lfn_index = 0xFFFF;
        dir_index = 0;
		attrib = 0;
		lfsize = namesize;
	    if (namesize) {
	        lfname = new char[namesize];
			lfname[0] = '$'; // test/debug
			lfname[1] = '\0';
		}
	    else
	        lfname = NULL;
        extension[3] = 0;
    }
    
    FileInfo(char *name)
    {
		fs = NULL;
		cluster = 0;
		size = 0;
		date = 0;
		time = 0;
		dir_clust = 0;
        dir_index = 0;
		attrib = 0;
		lfsize = strlen(name)+1;
        lfname = new char[lfsize];
        strcpy(lfname, name);
        extension[3] = 0;
    }
    
    FileInfo(FileInfo &i)
    {
        fs = i.fs;
        cluster = i.cluster;
        size = i.size;
        date = i.date;
        time = i.time;
        dir_clust = i.dir_clust;
        dir_index = i.dir_index;
//        dir_sector = i.dir_sector;
//        dir_offset = i.dir_offset;
//        lfn_index = i.lfn_index;
        lfsize = i.lfsize;
        lfname = new char[lfsize];
        strncpy(lfname, i.lfname, lfsize);
        strncpy(extension, i.extension, 4);
        attrib = i.attrib;
        extension[3] = 0;
    }
    
    FileInfo(FileInfo *i, char *new_name)
    {
        fs = i->fs;
        cluster = i->cluster;
        size = i->size;
        date = i->date;
        time = i->time;
        dir_clust = i->dir_clust;
        dir_index = i->dir_index;
//        dir_sector = i->dir_sector;
//        dir_offset = i->dir_offset;
//        lfn_index = i->lfn_index;
		lfsize = strlen(new_name)+1;
        lfname = new char[lfsize];
        strcpy(lfname, new_name);
        attrib = i->attrib;
        extension[0] = 0;
    }

	~FileInfo() {
	    if(lfname)
	        delete lfname;
    }

	bool is_directory(void) {
	    return (attrib & AM_DIR);
	}

	bool is_writable(void);
	    
	void print_info(void) {
		printf("File System: %p\n", fs);
		printf("Cluster    : %d\n", cluster);
		printf("Size       : %d\n", size);
		printf("Date       : %d\n", date);
		printf("Time	   : %d\n", time);
        printf("DIR index  : %d\n", dir_index);
		printf("LFSize     : %d\n", lfsize);
		printf("LFname     : %s\n", lfname);
		printf("Attrib:    : %b\n", attrib);
		printf("Extension  : %s\n", extension);
		printf("Dir Clust  : %d\n", dir_clust);
//        printf("LFN index  : %d\n", lfn_index);
//		printf("Dir sector : %d\n", dir_sector);
//		printf("Dir offset : %d\n", dir_offset);
	}
};


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

    static  bool check(Partition *p);        // check if file system is present on this partition
    virtual bool    init(void);              // Initialize file system
    virtual FRESULT get_free (DWORD *e) { *e = 0; return FR_OK; } // Get number of free sectors on the file system
    virtual bool is_writable() { return false; } // by default a file system is not writable, unless we implement it
    virtual FRESULT sync(void) { return FR_OK; } // by default we can't write, and syncing is thus always successful
    
    // functions for reading directories
    virtual Directory *dir_open(FileInfo *); // Opens directory (creates dir object, NULL = root)
    virtual void dir_close(Directory *d);    // Closes (and destructs dir object)
    virtual FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir
    virtual FRESULT dir_create(FileInfo *);  // Creates a directory as specified by finfo
    
    // functions for reading and writing files
    virtual File   *file_open(FileInfo *, BYTE flags);  // Opens file (creates file object)
    virtual FRESULT file_rename(FileInfo *, char *new_name); // Renames a file
	virtual FRESULT file_delete(FileInfo *); // deletes a file
    virtual void    file_close(File *f);                // Closes file (and destructs file object)
    virtual FRESULT file_read(File *f, void *buffer, DWORD len, UINT *transferred);
    virtual FRESULT file_write(File *f, void *buffer, DWORD len, UINT *transferred);
    virtual FRESULT file_seek(File *f, DWORD pos);
    virtual FRESULT file_sync(File *f);             // Clean-up cached data
    virtual void    file_print_info(File *f) { } // debug
    
};

// FATFS is a derivate from this base class. This base class implements the interface

#include "directory.h"
#include "path.h"
#include "file.h"

#endif
