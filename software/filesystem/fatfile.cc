    
extern "C" {
    #include "small_printf.h"
	#include "dump_hex.h"
}

#include "file_system.h"
#include "file.h"
#include "fatfile.h"
#include "fat_dir.h"
#include <string.h>
#include "rtc.h"

#define mem_set memset
#define mem_cmp memcmp
#define mem_cpy memcpy

DWORD get_fattime (void)	/* 31-25: Year(0-127 org.1980), 24-21: Month(1-12), 20-16: Day(1-31) */
						    /* 15-11: Hour(0-23), 10-5: Minute(0-59), 4-0: Second(0-29 *2) */
{
	return rtc.get_fat_time();

	/*
    29 << 25 = 0x3A000000
     4 << 21 = 0x00800000
     4 << 16 = 0x00040000
     9 << 11 = 0x00004800
    36 <<  5 = 0x00000480
    23 <<  0 = 0x00000017
    return 0x3A844C97;
*/    
}

/* Some definitions to make life easier, but the code less readable. Thanks, Chen! */

/*-----------------------------------------------------------------------*/
/* Constructors                                                          */
/*-----------------------------------------------------------------------*/
FATFIL::FATFIL(FATFS *pfs) /* default constructor */
{
    fs = pfs;
    valid = 0;
#if !_FS_READONLY
    dir_obj = NULL;
#endif
}

FATFIL::FATFIL(FATFS *pfs, XCHAR *path, BYTE mode)
{
    fs = pfs;
    WORD dummy;
#if !_FS_READONLY
    dir_obj = NULL;
#endif
    open(path, 0, mode, &dummy);
}

FATFIL::~FATFIL()
{
#if !_FS_READONLY
    if(dir_obj)
        delete dir_obj;
#endif
}
    
FRESULT FATFIL::validate(void)
{
    ENTER_FF(fs);

    if(!valid)
        return FR_NOT_READY;

    return fs->validate();
}
    

/*-----------------------------------------------------------------------*/
/* Open or Create a File                                                 */
/*-----------------------------------------------------------------------*/
FRESULT FATFIL::open (
    XCHAR *path,  /* Pointer to the file name */
    DWORD dir_clust,  /* if given, this is where we start with the path */
    BYTE mode,   /* Access mode and file open mode flags */
    WORD *dir_index  /* return value: index of dir entry that was found */
)
{
    FRESULT res;
    BYTE *dir;

    valid = 0;
    *dir_index = 0xFFFF;

#if !_FS_READONLY
    if(dir_obj)
        delete dir_obj;
    dir_obj = new FATDIR(fs, dir_clust);    /* Init FATDIR object and point to the file system */
#else
    FATDIR dj(fs, dir_clust);
    FATDIR *dir_obj = &dj;
#endif

#if !_FS_READONLY
    mode &= (FA_READ | FA_WRITE | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_CREATE_NEW);
    res = fs->chk_mounted(mode);
#else
    mode &= FA_READ;
    res = fs->chk_mounted(0);
#endif
    if (res != FR_OK) LEAVE_FF(fs, res);

    res = dir_obj->follow_path(path, dir_clust);   /* Follow the file path */
//    small_printf("Follow path (%s/%d) returned: %d.\n", path, dir_clust, res);

#if !_FS_READONLY
    /* Create or Open a file */
    if (mode & (FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_CREATE_NEW)) {
        DWORD ps, cl;

        if (res != FR_OK) {         /* No file, create new */
            if (res == FR_NO_FILE)  /* There is no file to open, create a new entry */
                res = dir_obj->dir_register();
            if (res != FR_OK) LEAVE_FF(fs, res);
            mode |= FA_CREATE_ALWAYS;
            dir = dir_obj->dir;           /* Created entry (SFN entry) */
        }
        else {                      /* Any object is already existing */
            if (mode & FA_CREATE_NEW)           /* Cannot create new */
                LEAVE_FF(fs, FR_EXIST);
            dir = dir_obj->dir;
            if (!dir || (dir[FATDIR_Attr] & (AM_RDO | AM_DIR)))    /* Cannot overwrite it (R/O or FATDIR) */
                LEAVE_FF(fs, FR_DENIED);
            if (mode & FA_CREATE_ALWAYS) {      /* Resize it to zero on over write mode */
                cl = ((DWORD)LD_WORD(dir+FATDIR_FstClusHI) << 16) | LD_WORD(dir+FATDIR_FstClusLO);    /* Get start cluster */
                ST_WORD(dir+FATDIR_FstClusHI, 0);  /* cluster = 0 */
                ST_WORD(dir+FATDIR_FstClusLO, 0);
                ST_DWORD(dir+FATDIR_FileSize, 0);  /* size = 0 */
                fs->wflag = 1;
                ps = fs->winsect;          /* Remove the cluster chain */
                if (cl) {
                    res = fs->remove_chain(cl);
                    if (res) LEAVE_FF(fs, res);
                    fs->last_clust = cl - 1;   /* Reuse the cluster hole */
                }
                res = fs->move_window(ps);
                if (res != FR_OK) LEAVE_FF(fs, res);
            }
        }
        if (mode & FA_CREATE_ALWAYS) {
            dir[FATDIR_Attr] = 0;                  /* Reset attribute */
            ps = get_fattime();
            ST_DWORD(dir+FATDIR_CrtTime, ps);      /* Created time */
            fs->wflag = 1;
            mode |= FA__WRITTEN;                /* Set file changed flag */
        }
    }
    /* Open an existing file */
    else {
#endif /* !_FS_READONLY */
        if (res != FR_OK) LEAVE_FF(fs, res);   /* Follow failed */
        dir = dir_obj->dir;
        if (!dir || (dir[FATDIR_Attr] & AM_DIR))   /* It is a directory */
            LEAVE_FF(fs, FR_NO_FILE);
#if !_FS_READONLY
        if ((mode & FA_WRITE) && (dir[FATDIR_Attr] & AM_RDO)) /* R/O violation */
            LEAVE_FF(fs, FR_DENIED);
    }
//    dir_sect = fs->winsect;        /* Pointer to the directory entry */
//    dir_ptr = dir_obj->dir;
//    dir_entry_known = 1;
    *dir_index = dir_obj->index;
#endif
    flag = mode;                    /* File access mode */
    org_clust =                     /* File start cluster */
        ((DWORD)LD_WORD(dir+FATDIR_FstClusHI) << 16) | LD_WORD(dir+FATDIR_FstClusLO);
    fsize = LD_DWORD(dir+FATDIR_FileSize); /* File size */
    fptr = 0; csect = 255;      /* File pointer */
    dsect = 0;

    valid = 1;

//    printf("File opened by name.. this is the directory info: \n");
//    dir_obj->print_info();
    LEAVE_FF(fs, FR_OK);
}


FRESULT FATFIL::open (
    FileInfo *info, /* File Info, as previously read from directory */
    BYTE mode       /* Access mode and file open mode flags */
)
{
    FRESULT res;
    WORD dir_index;
    
//    small_printf("File open: Flags=%b\n", mode);
//    info->print_info();

    valid = 0;

    /* Create or Open a file */
    if (mode & (FA_CREATE_ALWAYS | FA_CREATE_NEW | FA_OPEN_ALWAYS)) {
    	res = open(info->lfname, info->dir_clust, mode, &dir_index); // dispatch to the old style
    	if(res != FR_OK)
    		return res;
    	/* put information back into file info structure */
    	info->cluster = org_clust;
    	info->date = info->time = 0;
#if !_FS_READONLY
        info->dir_index  = dir_index;
//    	info->dir_sector = dir_sect;
//    	info->dir_offset = WORD(dir_ptr - fs->win);
#endif
    	info->size = 0;
    	return FR_OK;
    }

    /* Open an existing file */
    if (info->attrib & AM_DIR)   /* It is a directory */
        LEAVE_FF(fs, FR_NO_FILE);

    if ((mode & FA_WRITE) && (info->attrib & AM_RDO)) /* R/O violation */
        LEAVE_FF(fs, FR_DENIED);

#if !_FS_READONLY
    if(dir_obj) {
        printf("Warning: File structure already has dir object attached!\n");
        delete dir_obj;
    }
    dir_obj = new FATDIR(fs, info->dir_clust);
//    printf("Seeking Index %d of dir resulted in: %d\n", info->dir_index, dir_obj->dir_seek(info->dir_index));
#endif

    flag = mode;                    /* File access mode */
    org_clust = info->cluster;      /* File start cluster */
    fsize = info->size;             /* File size */
    fptr  = 0; csect = 255;         /* File pointer */
    dsect = 0;
    valid = 1;

//    print_info();
    LEAVE_FF(fs, FR_OK);
}


/*-----------------------------------------------------------------------*/
/* Read File                                                             */
/*-----------------------------------------------------------------------*/

FRESULT FATFIL::read (
    void *buff,     /* Pointer to data buffer */
    UINT btr,       /* Number of bytes to read */
    UINT *br        /* Pointer to number of bytes read */
)
{
    FRESULT res;
    DWORD clst, sect, remain;
    UINT rcnt, cc;
    BYTE *rbuff = (BYTE *)buff;


    *br = 0;    /* Initialize bytes read */

    res = validate();                           /* Check validity of the object */
    if (res != FR_OK) LEAVE_FF(fs, res);
    if (flag & FA__ERROR)                       /* Check abort flag */
        LEAVE_FF(fs, FR_INT_ERR);
    if (!(flag & FA_READ))                      /* Check access mode */
        LEAVE_FF(fs, FR_DENIED);
    remain = fsize - fptr;
    if (btr > remain) btr = (UINT)remain;           /* Truncate btr by remaining bytes */

    for ( ;  btr;                                   /* Repeat until all data transferred */
        rbuff += rcnt, fptr += rcnt, *br += rcnt, btr -= rcnt) {
        if ((fptr % REF_SECSIZE(fs)) == 0) {         /* On the sector boundary? */
            if (csect >= fs->csize) {       /* On the cluster boundary? */
                clst = (fptr == 0) ?            /* On the top of the file? */
                    org_clust : fs->get_fat(curr_clust);
                if (clst <= 1) ABORT(fs, FR_INT_ERR);
                if (clst == 0xFFFFFFFF) ABORT(fs, FR_DISK_ERR);
                curr_clust = clst;              /* Update current cluster */
                csect = 0;                      /* Reset sector offset in the cluster */
            }
            sect = fs->clust2sect(curr_clust);  /* Get current sector */
            if (!sect) ABORT(fs, FR_INT_ERR);
            sect += csect;
            cc = btr / REF_SECSIZE(fs);                  /* When remaining bytes >= sector size, */
            if (cc) {                               /* Read maximum contiguous sectors directly */
                if (csect + cc > fs->csize) /* Clip at cluster boundary */
                    cc = fs->csize - csect;
                if (fs->prt->read(rbuff, sect, (BYTE)cc) != RES_OK)
                    ABORT(fs, FR_DISK_ERR);
#if !_FS_READONLY && _FS_MINIMIZE <= 2
#if _FS_TINY
                if (fs->wflag && fs->winsect - sect < cc)       /* Replace one of the read sectors with cached data if it contains a dirty sector */
                    mem_cpy(rbuff + ((fs->winsect - sect) * REF_SECSIZE(fs)), fs->win, REF_SECSIZE(fs));
#else
                if ((flag & FA__DIRTY) && dsect - sect < cc)    /* Replace one of the read sectors with cached data if it contains a dirty sector */
                    mem_cpy(rbuff + ((dsect - sect) * REF_SECSIZE(fs)), buf, REF_SECSIZE(fs));
#endif
#endif
                csect += (BYTE)cc;              /* Next sector address in the cluster */
                rcnt = REF_SECSIZE(fs) * cc;             /* Number of bytes transferred */
                continue;
            }
#if !_FS_TINY
#if !_FS_READONLY
            if (flag & FA__DIRTY) {         /* Write sector I/O buffer if needed */
                if (fs->prt->write(buf, dsect, 1) != RES_OK)
                    ABORT(fs, FR_DISK_ERR);
                flag &= ~FA__DIRTY;
            }
#endif
            if (dsect != sect) {            /* Fill sector buffer with file data */
                if (fs->prt->read(buf, sect, 1) != RES_OK)
                    ABORT(fs, FR_DISK_ERR);
            }
#endif
            dsect = sect;
            csect++;                            /* Next sector address in the cluster */
        }
        rcnt = REF_SECSIZE(fs) - (fptr % REF_SECSIZE(fs));    /* Get partial sector data from sector buffer */
        if (rcnt > btr) rcnt = btr;
#if _FS_TINY
        if (move_window(fs, dsect))         /* Move sector window */
            ABORT(fs, FR_DISK_ERR);
        mem_cpy(rbuff, &fs->win[fptr % REF_SECSIZE(fs)], rcnt);  /* Pick partial sector */
#else
        mem_cpy(rbuff, &buf[fptr % REF_SECSIZE(fs)], rcnt);  /* Pick partial sector */
#endif
    }

    LEAVE_FF(fs, FR_OK);
}




#if !_FS_READONLY
/*-----------------------------------------------------------------------*/
/* Write File                                                            */
/*-----------------------------------------------------------------------*/

FRESULT FATFIL::write (
    const void *buff,   /* Pointer to the data to be written */
    UINT btw,           /* Number of bytes to write */
    UINT *bw            /* Pointer to number of bytes written */
)
{
    FRESULT res;
    DWORD clst, sect;
    UINT wcnt, cc;
    const BYTE *wbuff = (BYTE *)buff;

    *bw = 0;    /* Initialize bytes written */

    res = validate();                       /* Check validity of the object */
    if (res != FR_OK) LEAVE_FF(fs, res);

    if (flag & FA__ERROR)                   /* Check abort flag */
        LEAVE_FF(fs, FR_INT_ERR);
    if (!(flag & FA_WRITE))                 /* Check access mode */
        LEAVE_FF(fs, FR_DENIED);
    if (fsize + btw < fsize) btw = 0;       /* File size cannot reach 4GB */

    for ( ;  btw;                                   /* Repeat until all data transferred */
        wbuff += wcnt, fptr += wcnt, *bw += wcnt, btw -= wcnt) {
        if ((fptr % REF_SECSIZE(fs)) == 0) {         /* On the sector boundary? */
            if (csect >= fs->csize) {       /* On the cluster boundary? */
                if (fptr == 0) {                /* On the top of the file? */
                    clst = org_clust;           /* Follow from the origin */
                    if (clst == 0)                  /* When there is no cluster chain, */
                        org_clust = clst = fs->create_chain(0); /* Create a new cluster chain */
                } else {                            /* Middle or end of the file */
                    clst = fs->create_chain(curr_clust);            /* Follow or stretch cluster chain */
                }
                if (clst == 0) break;               /* Could not allocate a new cluster (disk full) */
                if (clst == 1) ABORT(fs, FR_INT_ERR);
                if (clst == 0xFFFFFFFF) ABORT(fs, FR_DISK_ERR);
                curr_clust = clst;              /* Update current cluster */
                csect = 0;                      /* Reset sector address in the cluster */
            }
#if _FS_TINY
            if (fs->winsect == dsect && fs->move_window(0)) /* Write back data buffer prior to following direct transfer */
                ABORT(fs, FR_DISK_ERR);
#else
            if (flag & FA__DIRTY) {     /* Write back data buffer prior to following direct transfer */
                if (fs->prt->write(buf, dsect, 1) != RES_OK)
                    ABORT(fs, FR_DISK_ERR);
                flag &= ~FA__DIRTY;
            }
#endif
            sect = fs->clust2sect(curr_clust);  /* Get current sector */
            if (!sect) ABORT(fs, FR_INT_ERR);
            sect += csect;
            cc = btw / REF_SECSIZE(fs);                  /* When remaining bytes >= sector size, */
            if (cc) {                               /* Write maximum contiguous sectors directly */
                if (csect + cc > fs->csize) /* Clip at cluster boundary */
                    cc = fs->csize - csect;
                if (fs->prt->write(wbuff, sect, (BYTE)cc) != RES_OK)
                    ABORT(fs, FR_DISK_ERR);
#if _FS_TINY
                if (fs->winsect - sect < cc) {  /* Refill sector cache if it gets dirty by the direct write */
                    mem_cpy(fs->win, wbuff + ((fs->winsect - sect) * REF_SECSIZE(fs)), REF_SECSIZE(fs));
                    fs->wflag = 0;
                }
#else
                if (dsect - sect < cc) {        /* Refill sector cache if it gets dirty by the direct write */
                    mem_cpy(buf, wbuff + ((dsect - sect) * REF_SECSIZE(fs)), REF_SECSIZE(fs));
                    flag &= ~FA__DIRTY;
                }
#endif
                csect += (BYTE)cc;              /* Next sector address in the cluster */
                wcnt = REF_SECSIZE(fs) * cc;             /* Number of bytes transferred */
                continue;
            }
#if _FS_TINY
            if (fptr >= fsize) {            /* Avoid silly buffer filling at growing edge */
                if (fs->move_window(0)) ABORT(fs, FR_DISK_ERR);
                fs->winsect = sect;
            }
#else
            if (dsect != sect) {                /* Fill sector buffer with file data */
                if (fptr < fsize &&
                    fs->prt->read(buf, sect, 1) != RES_OK)
                        ABORT(fs, FR_DISK_ERR);
            }
#endif
            dsect = sect;
            csect++;                            /* Next sector address in the cluster */
        }
        wcnt = REF_SECSIZE(fs) - (fptr % REF_SECSIZE(fs));    /* Put partial sector into file I/O buffer */
        if (wcnt > btw) wcnt = btw;
#if _FS_TINY
        if (fs->move_window(dsect))         /* Move sector window */
            ABORT(fs, FR_DISK_ERR);
        mem_cpy(&fs->win[fptr % REF_SECSIZE(fs)], wbuff, wcnt);  /* Fit partial sector */
        fs->wflag = 1;
#else
        mem_cpy(&buf[fptr % REF_SECSIZE(fs)], wbuff, wcnt);  /* Fit partial sector */
        flag |= FA__DIRTY;
#endif
    }

    if (fptr > fsize) fsize = fptr; /* Update file size if needed */
    flag |= FA__WRITTEN;                        /* Set file changed flag */

    LEAVE_FF(fs, FR_OK);
}

/*-----------------------------------------------------------------------*/
/* Synchronize the File Object                                           */
/*-----------------------------------------------------------------------*/

FRESULT FATFIL::sync (void)
{
    FRESULT res;
    DWORD tim;
    BYTE *dir;

    res = validate();
    if (res == FR_OK) {
        if (flag & FA__WRITTEN) {   /* Has the file been written? */
#if !_FS_TINY   /* Write-back dirty buffer */
            if (flag & FA__DIRTY) {
                if (fs->prt->write(buf, dsect, 1) != RES_OK)
                    LEAVE_FF(fs, FR_DISK_ERR);
                flag &= ~FA__DIRTY;
            }
#endif
            /* Update the directory entry */

            res = fs->move_window(dir_obj->sect);
            if (res == FR_OK) {
                dir = dir_obj->dir;
                dir[FATDIR_Attr] |= AM_ARC;                    /* Set archive bit */
                ST_DWORD(dir+FATDIR_FileSize, fsize);      /* Update file size */
                ST_WORD(dir+FATDIR_FstClusLO, org_clust);  /* Update start cluster */
                ST_WORD(dir+FATDIR_FstClusHI, org_clust >> 16);
                tim = get_fattime();            /* Updated time */
                ST_DWORD(dir+FATDIR_WrtTime, tim);
                flag &= ~FA__WRITTEN;
                fs->wflag = 1;
                res = fs->sync();
            }
        }
    }

    LEAVE_FF(fs, res);
}

#endif /* !_FS_READONLY */


/*-----------------------------------------------------------------------*/
/* Close File                                                            */
/*-----------------------------------------------------------------------*/

FRESULT FATFIL::close (void)
{
    FRESULT res;

#if _FS_READONLY
    res = validate();
    if (res == FR_OK)
        valid = 0;
    LEAVE_FF(fs, res);
#else
    res = sync();
    if (res == FR_OK)
        valid = 0;
    return res;
#endif
}



#if _FS_MINIMIZE <= 2
/*-----------------------------------------------------------------------*/
/* Seek File R/W Pointer                                                 */
/*-----------------------------------------------------------------------*/

FRESULT FATFIL::lseek (
    DWORD ofs       /* File pointer from top of file */
)
{
    FRESULT res;
    DWORD clst, bcs, nsect, ifptr;


    res = validate();                   /* Check validity of the object */
    if (res != FR_OK) LEAVE_FF(fs, res);
    if (flag & FA__ERROR)           /* Check abort flag */
        LEAVE_FF(fs, FR_INT_ERR);
    if (ofs > fsize                 /* In read-only mode, clip offset with the file size */
#if !_FS_READONLY
         && !(flag & FA_WRITE)
#endif
        ) ofs = fsize;

    ifptr = fptr;
    fptr = nsect = 0; csect = 255;
    if (ofs > 0) {
        bcs = (DWORD)fs->csize * REF_SECSIZE(fs);    /* Cluster size (byte) */
        if (ifptr > 0 &&
            (ofs - 1) / bcs >= (ifptr - 1) / bcs) { /* When seek to same or following cluster, */
            fptr = (ifptr - 1) & ~(bcs - 1);    /* start from the current cluster */
            ofs -= fptr;
            clst = curr_clust;
        } else {                                    /* When seek to back cluster, */
            clst = org_clust;                   /* start from the first cluster */
#if !_FS_READONLY
            if (clst == 0) {                        /* If no cluster chain, create a new chain */
                clst = fs->create_chain(0);
                if (clst == 1) ABORT(fs, FR_INT_ERR);
                if (clst == 0xFFFFFFFF) ABORT(fs, FR_DISK_ERR);
                org_clust = clst;
            }
#endif
            curr_clust = clst;
        }
        if (clst != 0) {
            while (ofs > bcs) {                     /* Cluster following loop */
#if !_FS_READONLY
                if (flag & FA_WRITE) {          /* Check if in write mode or not */
                    clst = fs->create_chain(clst);  /* Force stretched if in write mode */
                    if (clst == 0) {                /* When disk gets full, clip file size */
                        ofs = bcs; break;
                    }
                } else
#endif
                    clst = fs->get_fat(clst);   /* Follow cluster chain if not in write mode */
                if (clst == 0xFFFFFFFF) ABORT(fs, FR_DISK_ERR);
                if (clst <= 1 || clst >= fs->max_clust) ABORT(fs, FR_INT_ERR);
                curr_clust = clst;
                fptr += bcs;
                ofs -= bcs;
            }
            fptr += ofs;
            csect = (BYTE)(ofs / REF_SECSIZE(fs));   /* Sector offset in the cluster */
            if (ofs % REF_SECSIZE(fs)) {
                nsect = fs->clust2sect(clst);   /* Current sector */
                if (!nsect) ABORT(fs, FR_INT_ERR);
                nsect += csect;
                csect++;
            }
        }
    }
    if (fptr % REF_SECSIZE(fs) && nsect != dsect) {
#if !_FS_TINY
#if !_FS_READONLY
        if (flag & FA__DIRTY) {         /* Write-back dirty buffer if needed */
            if (fs->prt->write(buf, dsect, 1) != RES_OK)
                ABORT(fs, FR_DISK_ERR);
            flag &= ~FA__DIRTY;
        }
#endif
        if (fs->prt->read(buf, nsect, 1) != RES_OK)
            ABORT(fs, FR_DISK_ERR);
#endif
        dsect = nsect;
    }
#if !_FS_READONLY
    if (fptr > fsize) {         /* Set changed flag if the file size is extended */
        fsize = fptr;
        flag |= FA__WRITTEN;
    }
#endif

    LEAVE_FF(fs, res);
}


/*-----------------------------------------------------------------------*/
/* Truncate File                                                         */
/*-----------------------------------------------------------------------*/
FRESULT FATFIL::truncate (void)
{
    FRESULT res;
    DWORD ncl;

    res = validate();     /* Check validity of the object */
    if (res != FR_OK) LEAVE_FF(fs, res);
    if (flag & FA__ERROR)           /* Check abort flag */
        LEAVE_FF(fs, FR_INT_ERR);
    if (!(flag & FA_WRITE))         /* Check access mode */
        LEAVE_FF(fs, FR_DENIED);

    if (fsize > fptr) {
        fsize = fptr;   /* Set file size to current R/W point */
        flag |= FA__WRITTEN;
        if (fptr == 0) {    /* When set file size to zero, remove entire cluster chain */
            res = fs->remove_chain(org_clust);
            org_clust = 0;
        } else {                /* When truncate a part of the file, remove remaining clusters */
            ncl = fs->get_fat(curr_clust);
            res = FR_OK;
            if (ncl == 0xFFFFFFFF)
                res = FR_DISK_ERR;
            if (ncl == 1)
                res = FR_INT_ERR;
            if (res == FR_OK && ncl < fs->max_clust) {
                res = fs->put_fat(curr_clust, 0x0FFFFFFF);
                if (res == FR_OK)
                    res = fs->remove_chain(ncl);
            }
        }
    }
    if (res != FR_OK) flag |= FA__ERROR;

    LEAVE_FF(fs, res);
}

#endif /* MINIMIZE */

#if _USE_STRFUNC
/*-----------------------------------------------------------------------*/
/* Get a string from the file                                            */
/*-----------------------------------------------------------------------*/
char* FATFIL::fgets (
    char* buff, /* Pointer to the string buffer to read */
    int len    /* Size of string buffer */
)
{
    int i = 0;
    char *p = buff;
    UINT rc;


    while (i < len - 1) {           /* Read bytes until buffer gets filled */
        read(p, 1, &rc);
        if (rc != 1) break;         /* Break when no data to read */
#if _USE_STRFUNC >= 2
        if (*p == '\r') continue;   /* Strip '\r' */
#endif
        i++;
        if (*p++ == '\n') break;    /* Break when reached end of line */
    }
    *p = 0;
    return i ? buff : NULL;         /* When no data read (eof or error), return with error. */
}


#if !_FS_READONLY
#include <stdarg.h>
/*-----------------------------------------------------------------------*/
/* Put a character to the file                                           */
/*-----------------------------------------------------------------------*/
int FATFIL::fputc (
    int chr    /* A character to be output */
)
{
    UINT bw;
    char c;

#if _USE_STRFUNC >= 2
    if (chr == '\n') f_putc ('\r', fil);    /* LF -> CRLF conversion */
#endif
    if (!this) { /* Special value may be used to switch the destination to any other device */
        return EOF;
    }
    c = (char)chr;
    write(&c, 1, &bw);   /* Write a byte to the file */
    return bw ? chr : EOF;      /* Return the result */
}

/*-----------------------------------------------------------------------*/
/* Put a string to the file                                              */
/*-----------------------------------------------------------------------*/
int FATFIL::fputs (
    const char* str    /* Pointer to the string to be output */
)
{
    int n;

    for (n = 0; *str; str++, n++) {
        if (putc(*str) == EOF) return EOF;
    }
    return n;
}


/*-----------------------------------------------------------------------*/
/* Put a formatted string to the file                                    */
/*-----------------------------------------------------------------------*/
int FATFIL::fprintf (
    const char* str,    /* Pointer to the format string */
    ...                 /* Optional arguments... */
)
{
    va_list arp;
    UCHAR c, f, r;
    ULONG val;
    char s[16];
    int i, w, res, cc;

    va_start(arp, str);

    for (cc = res = 0; cc != EOF; res += cc) {
        c = *str++;
        if (c == 0) break;          /* End of string */
        if (c != '%') {             /* Non escape cahracter */
            cc = putc(c);
            if (cc != EOF) cc = 1;
            continue;
        }
        w = f = 0;
        c = *str++;
        if (c == '0') {             /* Flag: '0' padding */
            f = 1; c = *str++;
        }
        while (c >= '0' && c <= '9') {  /* Precision */
            w = w * 10 + (c - '0');
            c = *str++;
        }
        if (c == 'l') {             /* Prefix: Size is long int */
            f |= 2; c = *str++;
        }
        if (c == 's') {             /* Type is string */
            cc = puts(va_arg(arp, char*));
            continue;
        }
        if (c == 'c') {             /* Type is character */
            cc = putc(va_arg(arp, int));
            if (cc != EOF) cc = 1;
            continue;
        }
        r = 0;
        if (c == 'd') r = 10;       /* Type is signed decimal */
        if (c == 'u') r = 10;       /* Type is unsigned decimal */
        if (c == 'X') r = 16;       /* Type is unsigned hexdecimal */
        if (r == 0) break;          /* Unknown type */
        if (f & 2) {                /* Get the value */
            val = (ULONG)va_arg(arp, long);
        } else {
            val = (c == 'd') ? (ULONG)(long)va_arg(arp, int) : (ULONG)va_arg(arp, unsigned int);
        }
        /* Put numeral string */
        if (c == 'd') {
            if (val & 0x80000000) {
                val = 0 - val;
                f |= 4;
            }
        }
        i = sizeof(s) - 1; s[i] = 0;
        do {
            c = (UCHAR)(val % r + '0');
            if (c > '9') c += 7;
            s[--i] = c;
            val /= r;
        } while (i && val);
        if (i && (f & 4)) s[--i] = '-';
        w = sizeof(s) - 1 - w;
        while (i && i > w) s[--i] = (f & 1) ? '0' : ' ';
        cc = puts(&s[i]);
    }

    va_end(arp);
    return (cc == EOF) ? cc : res;
}

#endif /* !_FS_READONLY */

int FATFIL::feof(void)
{
    if(fptr == fsize)
        return 1;
    return 0;
}
        
int FATFIL::ferror(void)
{
    return (flag & FA__ERROR) ? 1 : 0;
}


#endif /* _USE_STRFUNC */

void FATFIL :: print_info(void)
{
	printf("** FATFIL **\n");
	printf("File system : %p\n", fs);         /* Pointer to the owner file system object */
	printf("Valid       : %b\n", valid);      /* Set to a non-zero value when file is successfully opened */
	printf("Flag        : %b\n", flag);       /* File status flags */
	printf("csect       : %b\n", csect);      /* Sector address in the cluster */
	printf("File pointer: %d\n", fptr);       /* File R/W pointer */
	printf("File Size   : %d\n", fsize);      /* File size */
	printf("Org cluster : %d\n", org_clust);  /* File start cluster */
	printf("Current clst: %d\n", curr_clust); /* Current cluster */
	printf("Cur. dsect  : %d\n", dsect);      /* Current data sector */
#if !_FS_READONLY
    if(dir_obj) {
        dir_obj->print_info();
    }
#endif
}
