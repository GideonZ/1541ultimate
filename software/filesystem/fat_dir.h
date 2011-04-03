#ifndef FATDIR_H
#define FATDIR_H

#include "integer.h"   /* Basic integer types */
#include "ffconf.h"    /* FatFs configuration options */
#include "directory.h" /* Defines base class for this derivative */
#include "file_system.h"
#include "fat_fs.h"    /* Definitions to access a fat system */
#include "multilang.h" /* Definitions for multiple language support (code pages) */


/* Directory object structure */
class FATDIR
{
    FATFS*  fs;         /* Pointer to the owner file system object */
    WORD    index;      /* Current read/write index number */
    DWORD   sclust;     /* Table start cluster (0:Static table) */
    DWORD   clust;      /* Current cluster */
    DWORD   sect;       /* Current sector */
    BYTE*   dir;        /* Pointer to the current SFN entry in the win[] */
    BYTE    fn[16];     /* Pointer to the SFN (in/out) {file[8],ext[3],status[1]} */
    BYTE    valid;      /* Indicates validity of open directory. */
#if _USE_LFN
    WCHAR*  lfn;        /* Pointer to the LFN working buffer */
    WORD    lfn_idx;    /* Last matched LFN index number (0xFFFF:No LFN) */
#endif

protected:
    void    get_fileinfo(FileInfo *fno); /* Convert directory into usable structure   */
    FRESULT validate(void);             /* Check if a directory is open and file system is ok */
    FRESULT dir_seek (WORD idx);        /* Directory handling - Seek directory index */
    FRESULT dir_next (bool);            /* Directory handling - Move directory index next */
    FRESULT dir_find (void);            /* Finds filename */
#if _USE_LFN
    FRESULT dir_find_lfn_start(void);   // will set lfn_index correctly, based on current position
#endif
    FRESULT dir_read (void);            /* Reads entry */
    FRESULT dir_register (void);	    /* Register an object to the directory       */
    FRESULT dir_remove (void);          /* Remove an object from the directory       */
    FRESULT create_name (XCHAR **path); /* Pick a segment and create the object name in directory form */
    FRESULT follow_path (XCHAR *, DWORD d=0); /* Follow a file path */

public:
    FATDIR(FATFS *, DWORD start=0);  /* constructor */
    ~FATDIR();        /* destructor */

    void    print_info(void);           /* Dump contents of FATDIR structure to console */
    FRESULT open (XCHAR *path);         /* Create a directory Object                 */
    FRESULT open (FileInfo *fno);       /* Open the directory based on an entry in the dir above */
    FRESULT read_entry(FileInfo *fno);  /* Read Directory Entry in Sequence          */
    FRESULT mkdir(XCHAR *name);			/* Create a new directory */

    friend class FATFS;
    friend class FATFIL;
};

#define FATDIR_Name            0
#define FATDIR_Attr            11
#define FATDIR_NTres           12
#define FATDIR_CrtTime         14
#define FATDIR_CrtDate         16
#define FATDIR_FstClusHI       20
#define FATDIR_WrtTime         22
#define FATDIR_WrtDate         24
#define FATDIR_FstClusLO       26
#define FATDIR_FileSize        28
#define LFATDIR_Ord            0
#define LFATDIR_Attr           11
#define LFATDIR_Type           12
#define LFATDIR_Chksum         13
#define LFATDIR_FstClusLO      26


/* Name status flags */
#define NS          11      /* Offset of name status byte */
#define NS_LOSS     0x01    /* Out of 8.3 format */
#define NS_LFN      0x02    /* Force to create LFN entry */
#define NS_LAST     0x04    /* Last segment */
#define NS_BODY     0x08    /* Lower case flag (body) */
#define NS_EXT      0x10    /* Lower case flag (ext) */
#define NS_DOT      0x20    /* Dot entry */

/* Unicode - OEM code conversion */
#if _USE_LFN
WCHAR ff_convert (WCHAR, UINT);
WCHAR ff_wtoupper (WCHAR);
#endif

#if _FS_REENTRANT
#if _USE_LFN == 1
#error Static LFN work area must not be used in re-entrant configuration.
#endif
#define ENTER_FF(fs)        { if (!lock_fs(fs)) return FR_TIMEOUT; }
#define LEAVE_FF(fs, res)   { unlock_fs(fs, res); return res; }

#else
#define ENTER_FF(fs)
#define LEAVE_FF(fs, res)   return res

#endif

#define ABORT(fs, res)      { flag |= FA__ERROR; LEAVE_FF(fs, res); }


#if _USE_LFN == 1   /* LFN with static LFN working buffer */
static
WCHAR LfnBuf[_MAX_LFN + 1];
#define NAMEBUF(sp,lp)  BYTE sp[12]; WCHAR *lp = LfnBuf
#define INITBUF(dj,sp,lp)   dj.fn = sp; dj.lfn = lp

#elif _USE_LFN > 1  /* LFN with dynamic LFN working buffer */
#define NAMEBUF(sp,lp)  BYTE sp[12]; WCHAR lbuf[_MAX_LFN + 1], *lp = lbuf
#define INITBUF(dj,sp,lp)   dj.fn = sp; dj.lfn = lp

#else               /* No LFN */
#define NAMEBUF(sp,lp)  BYTE sp[12]
#define INITBUF(dj,sp,lp)   dj.fn = sp

#endif


#endif
