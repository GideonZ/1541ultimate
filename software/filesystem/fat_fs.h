/*---------------------------------------------------------------------------/
/  FatFs - FAT file system module include file  R0.07e       (C)ChaN, 2009
/----------------------------------------------------------------------------/
/ FatFs module is a generic FAT file system module for small embedded systems.
/ This is a free software that opened for education, research and commercial
/ developments under license policy of following trems.
/
/  Copyright (C) 2009, ChaN, all right reserved.
/
/ * The FatFs module is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial product UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/ C++'ed by Gideon Zweijtzer Nov 2009
/ Definitions for access to FAT file system
/----------------------------------------------------------------------------*/

#ifndef _FAT_FS_H
#define _FAT_FS_H  0x007E

#include "integer.h" /* Basic integer types */
#include "ffconf.h"  /* FatFs configuration options */
#include "partition.h"
#include "file_system.h"
#include "directory.h"
#include "file.h"

/* Definitions corresponds to multiple sector size */
#if _MAX_SS == 512      /* Single sector size */
#define SECSIZE    512U
#define REF_SECSIZE(x) 512U

#elif _MAX_SS == 1024 || _MAX_SS == 2048 || _MAX_SS == 4096 /* Multiple sector size */
#define SECSIZE (s_size)  /* fill in member variable */
#define REF_SECSIZE(x) (x->s_size)
#else
#error Sector size must be 512, 1024, 2048 or 4096.

#endif

/* Type of file name on FatFs API */
#if _LFN_UNICODE && _USE_LFN
typedef WCHAR XCHAR;    /* Unicode */
#else
typedef char XCHAR;     /* SBCS, DBCS */
#endif


/* File system class structure */
class FATFS : public FileSystem
{
private:    
//    Partition *prt;     /* Pointer to partition class */  (now defined in base class)
    BYTE    fs_type;    /* FAT sub type */
    BYTE    csize;      /* Number of sectors per cluster */
    BYTE    n_fats;     /* Number of FAT copies */
    BYTE    wflag;      /* win[] dirty flag (1:must be written back) */
    BYTE    fsi_flag;   /* fsinfo dirty flag (1:must be written back) */
    WORD    n_rootdir;  /* Number of root directory entries (0 on FAT32) */
#if _FS_REENTRANT
    _SYNC_t sobj;       /* Identifier of sync object */
#endif
#if _MAX_SS != 512
    WORD    s_size;     /* Sector size */
#endif
    DWORD   last_clust; /* Last allocated cluster */
    DWORD   free_clust; /* Number of free clusters */
    DWORD   fsi_sector; /* fsinfo sector */

#if _FS_RPATH
    DWORD   cdir;       /* Current directory (0:root)*/
#endif
    DWORD   sects_fat;  /* Sectors per fat */
    DWORD   max_clust;  /* Maximum cluster# + 1. Number of clusters is max_clust - 2 */
    DWORD   fatbase;    /* FAT start sector */
    DWORD   dirbase;    /* Root directory start sector (Cluster# on FAT32) */
    DWORD   database;   /* Data start sector */
    DWORD   winsect;    /* Current sector appearing in the win[] */
    BYTE    win[_MAX_SS];/* Disk access window for Directory/FAT */


public:
    FATFS(Partition *p) : FileSystem(p) { }   /* Constructor */
    ~FATFS() { }                              /* Destructor */
    
    static BYTE check_fs (Partition *p);      /* Load boot record and check if it is an FAT boot record */
    bool    is_writable(void) { return true; } // ###
    bool    init (void);                      /* Initialize file system object based on boot record */
    void    print_info (void);                /* Print information to console about FAT filesystem */
    FRESULT getfree (DWORD*);                 /* Get number of free clusters on the drive */
#if _FS_READONLY != 1
    FRESULT sync(void);                       /* Clean-up cached data */
#endif

#ifndef BOOTLOADER
    // functions for reading directories
    Directory *dir_open(FileInfo *);  // Opens directory (creates dir object, NULL = root)
    void    dir_close(Directory *d);  // Closes (and destructs dir object)
    FRESULT dir_read(Directory *d, FileInfo *f); // reads next entry from dir
    FRESULT dir_create(FileInfo *f);  // Creates a directory as specified by finfo
    
    // functions for reading and writing files
    File   *file_open(FileInfo *, BYTE flags);  // Opens file (creates file object)
    void    file_close(File *f);                // Closes file (and destructs file object)
    FRESULT file_rename(FileInfo *, char *new_name); // Renames a file
	FRESULT file_delete(FileInfo *); // deletes a file
    FRESULT file_read(File *f, void *buffer, DWORD len, UINT *transferred);
    FRESULT file_write(File *f, void *buffer, DWORD len, UINT *transferred);
    FRESULT file_seek(File *f, DWORD pos);
    FRESULT file_sync(File *f);
    void    file_print_info(File *f); // debug
#endif

private:
    FRESULT move_window(DWORD sector);        /* Sector number to make apperance in the fs->win[] */
    DWORD   get_fat(DWORD clst);              /* FAT access - Read value of a FAT entry */    
    FRESULT put_fat (DWORD clst, DWORD val);  /* FAT access - Write value of a FAT entry */
    FRESULT remove_chain (DWORD clst);        /* FAT handling - Remove a cluster chain */
    DWORD   create_chain (DWORD clst);        /* FAT handling - Stretch or Create a cluster chain */
    DWORD   clust2sect (DWORD clst);          /* Get sector# from cluster# */
    FRESULT validate (void);                  /* Check if the file system object is valid or not */
    FRESULT chk_mounted (BYTE chk_wp);        /* Make sure that the file system or object using it is valid */
    FRESULT f_mkfs (BYTE, BYTE, WORD);        /* Create a file system on the drive */
    FRESULT f_rename (FileInfo *, char *);    // new function for renaming

    friend class FATDIR;
    friend class FATFIL;
};



/*--------------------------------------------------------------*/
/* Flags and offset address                                     */


/* FAT sub type (FATFS.fs_type) */

#define FS_FAT12    1
#define FS_FAT16    2
#define FS_FAT32    3


/* FatFs refers the members in the FAT structures with byte offset instead
/ of structure member because there are incompatibility of the packing option
/ between various compilers. */

#define BS_jmpBoot          0
#define BS_OEMName          3
#define BPB_BytsPerSec      11
#define BPB_SecPerClus      13
#define BPB_RsvdSecCnt      14
#define BPB_NumFATs         16
#define BPB_RootEntCnt      17
#define BPB_TotSec16        19
#define BPB_Media           21
#define BPB_FATSz16         22
#define BPB_SecPerTrk       24
#define BPB_NumHeads        26
#define BPB_HiddSec         28
#define BPB_TotSec32        32
#define BS_55AA             510

#define BS_DrvNum           36
#define BS_BootSig          38
#define BS_VolID            39
#define BS_VolLab           43
#define BS_FilSysType       54

#define BPB_FATSz32         36
#define BPB_ExtFlags        40
#define BPB_FSVer           42
#define BPB_RootClus        44
#define BPB_FSInfo          48
#define BPB_BkBootSec       50
#define BS_DrvNum32         64
#define BS_BootSig32        66
#define BS_VolID32          67
#define BS_VolLab32         71
#define BS_FilSysType32     82

#define FSI_LeadSig         0
#define FSI_StrucSig        484
#define FSI_Free_Count      488
#define FSI_Nxt_Free        492

#define MBR_Table           446

#endif /* _FATFS */
