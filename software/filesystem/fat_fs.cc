/*----------------------------------------------------------------------------/
/  FatFs - FAT file system module  R0.07e                    (C)ChaN, 2009
/-----------------------------------------------------------------------------/
/ FatFs module is a generic FAT file system module for small embedded systems.
/ This is a free software that opened for education, research and commercial
/ developments under license policy of following trems.
/
/  Copyright (C) 2009, ChaN, all right reserved.
/
/ * The FatFs module is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/-----------------------------------------------------------------------------/
/ Feb 26,'06 R0.00  Prototype.
/
/ Apr 29,'06 R0.01  First stable version.
/
/ Jun 01,'06 R0.02  Added FAT12 support.
/                   Removed unbuffered mode.
/                   Fixed a problem on small (<32M) patition.
/ Jun 10,'06 R0.02a Added a configuration option (_FS_MINIMUM).
/
/ Sep 22,'06 R0.03  Added f_rename().
/                   Changed option _FS_MINIMUM to _FS_MINIMIZE.
/ Dec 11,'06 R0.03a Improved cluster scan algolithm to write files fast.
/                   Fixed f_mkdir() creates incorrect directory on FAT32.
/
/ Feb 04,'07 R0.04  Supported multiple drive system.
/                   Changed some interfaces for multiple drive system.
/                   Changed f_mountdrv() to f_mount().
/                   Added f_mkfs().
/ Apr 01,'07 R0.04a Supported multiple partitions on a plysical drive.
/                   Added a capability of extending file size to f_lseek().
/                   Added minimization level 3.
/                   Fixed an endian sensitive code in f_mkfs().
/ May 05,'07 R0.04b Added a configuration option _USE_NTFLAG.
/                   Added FSInfo support.
/                   Fixed DBCS name can result FR_INVALID_NAME.
/                   Fixed short seek (<= csize) collapses the file object.
/
/ Aug 25,'07 R0.05  Changed arguments of f_read(), f_write() and f_mkfs().
/                   Fixed f_mkfs() on FAT32 creates incorrect FSInfo.
/                   Fixed f_mkdir() on FAT32 creates incorrect directory.
/ Feb 03,'08 R0.05a Added f_truncate() and f_utime().
/                   Fixed off by one error at FAT sub-type determination.
/                   Fixed btr in f_read() can be mistruncated.
/                   Fixed cached sector is not flushed when create and close
/                   without write.
/
/ Apr 01,'08 R0.06  Added fputc(), fputs(), fprintf() and fgets().
/                   Improved performance of f_lseek() on moving to the same
/                   or following cluster.
/
/ Apr 01,'09 R0.07  Merged Tiny-FatFs as a buffer configuration option.
/                   Added long file name support.
/                   Added multiple code page support.
/                   Added re-entrancy for multitask operation.
/                   Added auto cluster size selection to f_mkfs().
/                   Added rewind option to f_readdir().
/                   Changed result code of critical errors.
/                   Renamed string functions to avoid name collision.
/ Apr 14,'09 R0.07a Separated out OS dependent code on reentrant cfg.
/                   Added multiple sector size support.
/ Jun 21,'09 R0.07c Fixed f_unlink() can return FR_OK on error.
/                   Fixed wrong cache control in f_lseek().
/                   Added relative path feature.
/                   Added f_chdir() and f_chdrive().
/                   Added proper case conversion to extended char.
/ Nov 03,'09 R0.07e Separated out configuration options from ff.h to ffconf.h.
/                   Fixed f_unlink() fails to remove a sub-dir on _FS_RPATH.
/                   Fixed name matching error on the 13 char boundary.
/                   Added a configuration option, _LFN_UNICODE.
/                   Changed f_readdir() to return the SFN with always upper
/                   case on non-LFN cfg.
/
/ C++'ed by Gideon Zweijtzer Nov 2009
/
/ Split into different files, one per class.
/ This file handles the file system operations itself, such as
/ fat chaining and all.
/---------------------------------------------------------------------------*/

extern "C" {
    #include "small_printf.h"
    #include "dump_hex.h"
}
#include "fat_fs.h"         /* FatFs configurations and declarations */
#include "fat_dir.h"
#include "fatfile.h"
#include <string.h>

#define mem_set memset


/*
FileInfo::FileInfo(int lfn_length)    // create object with this work area size
{
#if _USE_LFN
    lfname = new XCHAR[lfn_length];
    if(lfname)
        lfsize = lfn_length;
    else
        lfsize = 0;
#endif
    
    fsize = 0L;
    fdate = 0;
    ftime = 0;
    fattrib = 0;
    fname[0] = '\0';
}

FILINFO::~FILINFO()
{
#if _USE_LFN
    if (lfname)
        delete[] lfname;
#endif
}
*/

/*
FATFS::FATFS(Partition *p)
{
    prt = p;
}

FATFS::~FATFS(void)
{
    // nothing to clean up
}
*/

/*-----------------------------------------------------------------------*/
/* Change window offset                                                  */
/*-----------------------------------------------------------------------*/
FRESULT FATFS::move_window (
    DWORD sector    /* Sector number to make apperance in the win[] */
)                   /* Move to zero only writes back dirty window */
{
    DWORD wsect;

    wsect = winsect;
    if (wsect != sector) {  /* Changed current window */
#if !_FS_READONLY
        if (wflag) {    /* Write back dirty window if needed */
            if (prt->write(win, wsect, 1) != RES_OK)
                return FR_DISK_ERR;
            wflag = 0;
            if (wsect < (fatbase + sects_fat)) {    /* In FAT area */
                BYTE nf;
                for (nf = n_fats; nf > 1; nf--) {   /* Refrect the change to all FAT copies */
                    wsect += sects_fat;
                    prt->write(win, wsect, 1);
                }
            }
        }
#endif
        if (sector) {
            if (prt->read(win, sector, 1) != RES_OK)
                return FR_DISK_ERR;
            winsect = sector;
        }
    }
    return FR_OK;
}

/*-----------------------------------------------------------------------*/
/* Clean-up cached data                                                  */
/*-----------------------------------------------------------------------*/
#if !_FS_READONLY
FRESULT FATFS::sync(void)   /* FR_OK: successful, FR_DISK_ERR: failed */
{
    FRESULT res;

    res = move_window(0);
    if (res == FR_OK) {
        /* Update FSInfo sector if needed */
        if (fs_type == FS_FAT32 && fsi_flag) {
            winsect = 0;
            mem_set(win, 0, 512);
            ST_WORD(win+BS_55AA, 0xAA55);
            ST_DWORD(win+FSI_LeadSig, 0x41615252);
            ST_DWORD(win+FSI_StrucSig, 0x61417272);
            ST_DWORD(win+FSI_Free_Count, free_clust);
            ST_DWORD(win+FSI_Nxt_Free, last_clust);
            prt->write(win, fsi_sector, 1);
            fsi_flag = 0;
        }
        /* Make sure that no pending write process in the physical drive */
        if (prt->ioctl(CTRL_SYNC, (void*)NULL) != RES_OK)
            res = FR_DISK_ERR;
    }

    return res;
}
#endif


/*-----------------------------------------------------------------------*/
/* FAT access - Read value of a FAT entry                                */
/*-----------------------------------------------------------------------*/
DWORD FATFS::get_fat (  /* 0xFFFFFFFF:Disk error, 1:Interal error, Else:Cluster status */
    DWORD clst  /* Cluster# to get the link information */
)
{
    UINT wc, bc;
    DWORD fsect;

    if (clst < 2 || clst >= max_clust)  /* Range check */
        return 1;

    fsect = fatbase;
    switch (fs_type) {
    case FS_FAT12 :
        bc = clst; bc += bc / 2;
        if (move_window(fsect + (bc / SECSIZE))) break;
        wc = win[bc & (SECSIZE - 1)]; bc++;
        if (move_window(fsect + (bc / SECSIZE))) break;
        wc |= (WORD)win[bc & (SECSIZE - 1)] << 8;
        return (clst & 1) ? (wc >> 4) : (wc & 0xFFF);

    case FS_FAT16 :
        if (move_window(fsect + (clst / (SECSIZE / 2)))) break;
        return LD_WORD(&win[((WORD)clst * 2) & (SECSIZE - 1)]);

    case FS_FAT32 :
        if (move_window(fsect + (clst / (SECSIZE / 4)))) break;
        return LD_DWORD(&win[((WORD)clst * 4) & (SECSIZE - 1)]) & 0x0FFFFFFF;
    }

    return 0xFFFFFFFF;  /* An error occured at the disk I/O layer */
}


/*-----------------------------------------------------------------------*/
/* FAT access - Change value of a FAT entry                              */
/*-----------------------------------------------------------------------*/
#if !_FS_READONLY

FRESULT FATFS::put_fat (
    DWORD clst, /* Cluster# to be changed in range of 2 to max_clust - 1 */
    DWORD val   /* New value to mark the cluster */
)
{
    UINT bc;
    BYTE *p;
    DWORD fsect;
    FRESULT res;


    if (clst < 2 || clst >= max_clust) {    /* Range check */
        res = FR_INT_ERR;

    } else {
        fsect = fatbase;
        switch (fs_type) {
        case FS_FAT12 :
            bc = clst; bc += bc / 2;
            res = move_window(fsect + (bc / SECSIZE));
            if (res != FR_OK) break;
            p = &win[bc & (SECSIZE - 1)];
            *p = (clst & 1) ? ((*p & 0x0F) | ((BYTE)val << 4)) : (BYTE)val;
            bc++;
            wflag = 1;
            res = move_window(fsect + (bc / SECSIZE));
            if (res != FR_OK) break;
            p = &win[bc & (SECSIZE - 1)];
            *p = (clst & 1) ? (BYTE)(val >> 4) : ((*p & 0xF0) | ((BYTE)(val >> 8) & 0x0F));
            break;

        case FS_FAT16 :
            res = move_window(fsect + (clst / (SECSIZE / 2)));
            if (res != FR_OK) break;
            ST_WORD(&win[((WORD)clst * 2) & (SECSIZE - 1)], (WORD)val);
            break;

        case FS_FAT32 :
            res = move_window(fsect + (clst / (SECSIZE / 4)));
            if (res != FR_OK) break;
            ST_DWORD(&win[((WORD)clst * 4) & (SECSIZE - 1)], val);
            break;

        default :
            res = FR_INT_ERR;
        }
        wflag = 1;
    }

    return res;
}
#endif /* !_FS_READONLY */


/*-----------------------------------------------------------------------*/
/* FAT handling - Remove a cluster chain                                 */
/*-----------------------------------------------------------------------*/
#if !_FS_READONLY
FRESULT FATFS::remove_chain (
    DWORD clst          /* Cluster# to remove a chain from */
)
{
    FRESULT res;
    DWORD nxt;


    if (clst < 2 || clst >= max_clust) {    /* Check the range of cluster# */
        res = FR_INT_ERR;

    } else {
        res = FR_OK;
        while (clst < max_clust) {          /* Not a last link? */
            nxt = get_fat(clst);            /* Get cluster status */
            if (nxt == 0) break;                /* Empty cluster? */
            if (nxt == 1) { res = FR_INT_ERR; break; }  /* Internal error? */
            if (nxt == 0xFFFFFFFF) { res = FR_DISK_ERR; break; }    /* Disk error? */
            res = put_fat(clst, 0);         /* Mark the cluster "empty" */
            if (res != FR_OK) break;
            if (free_clust != 0xFFFFFFFF) { /* Update FSInfo */
                free_clust++;
                fsi_flag = 1;
            }
            clst = nxt; /* Next cluster */
        }
    }
    return res;
}
#endif


/*-----------------------------------------------------------------------*/
/* FAT handling - Stretch or Create a cluster chain                      */
/*-----------------------------------------------------------------------*/
#if !_FS_READONLY
DWORD FATFS::create_chain ( /* 0:No free cluster, 1:Internal error, 0xFFFFFFFF:Disk error, >=2:New cluster# */
    DWORD clst          /* Cluster# to stretch. 0 means create a new chain. */
)
{
    DWORD cs, ncl, scl, mcl;

    mcl = max_clust;
    if (clst == 0) {        /* Create new chain */
        scl = last_clust;           /* Get suggested start point */
        if (scl == 0 || scl >= mcl) scl = 1;
    }
    else {                  /* Stretch existing chain */
        cs = get_fat(clst);         /* Check the cluster status */
        if (cs < 2) return 1;           /* It is an invalid cluster */
        if (cs < mcl) return cs;        /* It is already followed by next cluster */
        scl = clst;
    }

    ncl = scl;              /* Start cluster */
    for (;;) {
        ncl++;                          /* Next cluster */
        if (ncl >= mcl) {               /* Wrap around */
            ncl = 2;
            if (ncl > scl) return 0;    /* No free custer */
        }
        cs = get_fat(ncl);          /* Get the cluster status */
        if (cs == 0) break;             /* Found a free cluster */
        if (cs == 0xFFFFFFFF || cs == 1)/* An error occured */
            return cs;
        if (ncl == scl) return 0;       /* No free custer */
    }

    if (put_fat(ncl, 0x0FFFFFFF))   /* Mark the new cluster "in use" */
        return 0xFFFFFFFF;
    if (clst != 0) {                    /* Link it to the previous one if needed */
        if (put_fat(clst, ncl))
            return 0xFFFFFFFF;
    }

    last_clust = ncl;               /* Update FSINFO */
    if (free_clust != 0xFFFFFFFF) {
        free_clust--;
        fsi_flag = 1;
    }

    return ncl;     /* Return new cluster number */
}
#endif /* !_FS_READONLY */




/*-----------------------------------------------------------------------*/
/* Get sector# from cluster#                                             */
/*-----------------------------------------------------------------------*/
DWORD FATFS::clust2sect (  /* !=0: Sector number, 0: Failed - invalid cluster# */
    DWORD clst      /* Cluster# to be converted */
)
{
    clst -= 2;
    if (clst >= (max_clust - 2)) return 0;      /* Invalid cluster# */
    return clst * csize + database;
}

/*-----------------------------------------------------------------------*/
/* Make sure that the file system or object using it is valid            */
/*-----------------------------------------------------------------------*/
/* FR_OK(0): successful, !=0: any error occured */
FRESULT FATFS::chk_mounted (
    BYTE chk_wp         /* !=0: Check media write protection for write access */
)
{
    DSTATUS stat;

    if (!this)
        return FR_NOT_ENABLED;   /* Is the file system object available? */

    if (fs_type) {              /* If the logical drive has been mounted */
        stat = prt->status();
        if (!(stat & STA_NOINIT)) { /* and the physical drive is kept initialized (has not been changed), */
#if !_FS_READONLY
            if (chk_wp && (stat & STA_PROTECT)) /* Check write protection if needed */
                return FR_WRITE_PROTECTED;
#endif
            return FR_OK;           /* The file system object is valid */
        }
    }
    return FR_NO_FILESYSTEM;
}

/*-----------------------------------------------------------------------*/
/* Load boot record and check if it is an FAT boot record                */
/*-----------------------------------------------------------------------*/
/* 0:The FAT boot record,
   1:Valid boot record but not an FAT
   2:Not a boot record
   3:Error */
BYTE FATFS::check_fs (Partition *prt)
{
    static BYTE my_win[512];
    
	if (prt->read(my_win, 0, 1) != RES_OK)	/* Load boot record */
		return 3;
/*
    printf("Loaded boot sector:\n");
    for(int i=0;i<512;i++) {
        printf("%b ", my_win[i]);
        if((i & 15)==15)
            printf("\n");
    }
*/
	if (LD_WORD(&my_win[BS_55AA]) != 0xAA55)	/* Check record signature (always placed at offset 510 even if the sector size is >512) */
		return 2;

	if ((LD_DWORD(&my_win[BS_FilSysType]) & 0xFFFFFF) == 0x544146)	/* Check "FAT" string */
		return 0;
	if ((LD_DWORD(&my_win[BS_FilSysType32]) & 0xFFFFFF) == 0x544146)
		return 0;

	return 1;
}

/*-----------------------------------------------------------------------*/
/* Initialize File system object, based on current boot record           */
/*-----------------------------------------------------------------------*/
bool FATFS::init(void)
{
    DWORD fsize, tsect, mclst;
    
	if (prt->read(win, 0, 1) != RES_OK)	/* Load boot record */
		return false;

	/* Initialize the file system object */
	fsize = LD_WORD(win+BPB_FATSz16);				/* Number of sectors per FAT */
	if (!fsize) fsize = LD_DWORD(win+BPB_FATSz32);
	sects_fat = fsize;
	n_fats = win[BPB_NumFATs];					/* Number of FAT copies */
	fsize *= n_fats;							/* (Number of sectors in FAT area) */
	fatbase = LD_WORD(win+BPB_RsvdSecCnt);      /* FAT start sector (lba) */
	csize = win[BPB_SecPerClus];				/* Number of sectors per cluster */
	n_rootdir = LD_WORD(win+BPB_RootEntCnt);	/* Nmuber of root directory entries */
	tsect = LD_WORD(win+BPB_TotSec16);			/* Number of sectors on the volume */
	if (!tsect) tsect = LD_DWORD(win+BPB_TotSec32);
	max_clust = mclst = (tsect					/* Last cluster# + 1 (Number of clusters + 2) */
		- LD_WORD(win+BPB_RsvdSecCnt) - fsize - n_rootdir / (SECSIZE/32)
		) / csize + 2;

	fs_type = FS_FAT12;										/* Determine the FAT sub type */
	if (mclst >= 0xFF7) fs_type = FS_FAT16;					/* Number of clusters >= 0xFF5 */
	if (mclst >= 0xFFF7) fs_type = FS_FAT32;				/* Number of clusters >= 0xFFF5 */

	if (fs_type == FS_FAT32)
		dirbase = LD_DWORD(win+BPB_RootClus);	/* Root directory start cluster */
	else
		dirbase = fatbase + fsize;				/* Root directory start sector (lba) */
	database = fatbase + fsize + n_rootdir / (SECSIZE/32);	/* Data start sector (lba) */

	/* Initialize allocation information */
	free_clust = 0xFFFFFFFF;
	wflag = 0;
	/* Get fsinfo if needed */
	if (fs_type == FS_FAT32) {
	 	fsi_flag = 0;
		fsi_sector = LD_WORD(win+BPB_FSInfo);
		if (prt->read(win, fsi_sector, 1) == RES_OK &&
			LD_WORD(win+BS_55AA) == 0xAA55 &&
			LD_DWORD(win+FSI_LeadSig) == 0x41615252 &&
			LD_DWORD(win+FSI_StrucSig) == 0x61417272) {
			last_clust = LD_DWORD(win+FSI_Nxt_Free);
			free_clust = LD_DWORD(win+FSI_Free_Count);
		}
	}
	winsect = 0;		/* Invalidate sector cache */
#if _FS_RPATH
	cdir = 0;			/* Current directory (root dir) */
#endif
    return true;
}

void FATFS::print_info(void)
{
    printf("FAT type:      %d\n", fs_type);
    printf("Sec/cluster:   %d\n", csize);
    printf("# of FATs:     %d\n", n_fats);
    printf("Win Dirty:     %d\n", wflag);
    printf("FSI Dirty:     %d\n", fsi_flag);
    printf("Root entries:  %d\n", n_rootdir);
#if _MAX_SS != 512
    printf("Sector size:   %d\n", s_size);
#endif
    printf("Max cluster:   %d\n", max_clust);
#if !_FS_READONLY
    printf("Last cluster:  %d\n", last_clust);
    printf("Free clusters: %d\n", free_clust);
    printf("FSI sector:    %d\n", fsi_sector);
#endif
    printf("Sectors / FAT: %d\n", sects_fat);
    printf("Fat Base:      %d\n", fatbase);
    printf("Dir Base:      %d\n", dirbase);
    printf("Data Base:     %d\n", database);
}
        


/*-----------------------------------------------------------------------*/
/* Check if the file system objectis valid or not                        */
/*-----------------------------------------------------------------------*/

/* FR_OK(0): The object is valid, !=0: Invalid */
FRESULT FATFS::validate (void)
{
    if (!this)
        return FR_INVALID_OBJECT;

    if(!fs_type)
        return FR_INVALID_OBJECT;

    if (prt->status() & STA_NOINIT)
        return FR_NOT_READY;

    return FR_OK;
}

#if !_FS_READONLY
/*-----------------------------------------------------------------------*/
/* Get Number of Free Clusters                                           */
/*-----------------------------------------------------------------------*/

FRESULT FATFS::getfree (
    DWORD *nclst       /* Pointer to the variable to return number of free clusters */
)
{
    FRESULT res;
    DWORD n, clst, sect, stat;
    UINT i;
    BYTE fat, *p;


    /* Get drive number */
    res = chk_mounted(0);
    if (res != FR_OK)
        return res;

    /* If number of free cluster is valid, return it without cluster scan. */
    if (free_clust <= max_clust - 2) {
        *nclst = free_clust;
        return FR_OK;
    }

    /* Get number of free clusters */
    fat = fs_type;
    n = 0;
    if (fat == FS_FAT12) {
        clst = 2;
        do {
            stat = get_fat(clst);
            if (stat == 0xFFFFFFFF)
                return FR_DISK_ERR;
            if (stat == 1)
                return FR_INT_ERR;
            if (stat == 0)
                n++;
        } while (++clst < max_clust);
    } else {
        clst = max_clust;
        sect = fatbase;
        i = 0; p = 0;
        do {
            if (!i) {
                res = move_window(sect++);
                if (res != FR_OK)
                    return res;
                p = win;
                i = SECSIZE;
            }
            if (fat == FS_FAT16) {
                if (LD_WORD(p) == 0) n++;
                p += 2; i -= 2;
            } else {
                if ((LD_DWORD(p) & 0x0FFFFFFF) == 0) n++;
                p += 4; i -= 4;
            }
        } while (--clst);
    }
    free_clust = n;
    if (fat == FS_FAT32)
        fsi_flag = 1;
    *nclst = n;

    return FR_OK;
}
#endif

#if 0
#if _USE_MKFS && !_FS_READONLY
/*-----------------------------------------------------------------------*/
/* Create File System on the Drive                                       */
/*-----------------------------------------------------------------------*/
#define N_ROOTDIR   512         /* Multiple of 32 and <= 2048 */
#define N_FATS      1           /* 1 or 2 */
#define MAX_SECTOR  131072000UL /* Maximum partition size */
#define MIN_SECTOR  2000UL      /* Minimum partition size */


FRESULT FATFS::format (
    BYTE partition,     /* Partitioning rule 0:FDISK, 1:SFD */
    WORD allocsize      /* Allocation unit size [bytes] */
)
{
    static const DWORD sstbl[] = { 2048000, 1024000, 512000, 256000, 128000, 64000, 32000, 16000, 8000, 4000,   0 };
    static const WORD cstbl[] =  {   32768,   16384,   8192,   4096,   2048, 16384,  8192,  4096, 2048, 1024, 512 };
    BYTE fmt, m, *tbl;
    DWORD b_part, b_fat, b_dir, b_data;     /* Area offset (LBA) */
    DWORD n_part, n_rsv, n_fat, n_dir;      /* Area size */
    DWORD n_clst, d, n;
    WORD as;
    FATFS *fs;
    DSTATUS stat;


    /* Check validity of the parameters */
    if (partition >= 2)
        return FR_MKFS_ABORTED;

    /* Check mounted drive and clear work area */
    if (!this)
        return FR_NOT_ENABLED;
    
    fs_type = 0;

    /* Get disk statics */
    stat = disk_initialize(drv);
    if (stat & STA_NOINIT) return FR_NOT_READY;
    if (stat & STA_PROTECT) return FR_WRITE_PROTECTED;
#if _MAX_SS != 512                      /* Get disk sector size */
    if (fs->prt->ioctl(GET_SECTOR_SIZE, &REF_SECSIZE(fs)) != RES_OK
        || REF_SECSIZE(fs) > _MAX_SS)
        return FR_MKFS_ABORTED;
#endif
    if (fs->prt->ioctl(GET_SECTOR_COUNT, &n_part) != RES_OK || n_part < MIN_SECTOR)
        return FR_MKFS_ABORTED;
    if (n_part > MAX_SECTOR) n_part = MAX_SECTOR;
    b_part = (!partition) ? 63 : 0;     /* Boot sector */
    n_part -= b_part;
    for (d = 512; d <= 32768U && d != allocsize; d <<= 1) ; /* Check validity of the allocation unit size */
    if (d != allocsize) allocsize = 0;
    if (!allocsize) {                   /* Auto selection of cluster size */
        d = n_part;
        for (as = REF_SECSIZE(fs); as > 512U; as >>= 1) d >>= 1;
        for (n = 0; d < sstbl[n]; n++) ;
        allocsize = cstbl[n];
    }
    if (allocsize < REF_SECSIZE(fs)) allocsize = REF_SECSIZE(fs);

    allocsize /= REF_SECSIZE(fs);        /* Number of sectors per cluster */

    /* Pre-compute number of clusters and FAT type */
    n_clst = n_part / allocsize;
    fmt = FS_FAT12;
    if (n_clst >= 0xFF5) fmt = FS_FAT16;
    if (n_clst >= 0xFFF5) fmt = FS_FAT32;

    /* Determine offset and size of FAT structure */
    switch (fmt) {
    case FS_FAT12:
        n_fat = ((n_clst * 3 + 1) / 2 + 3 + REF_SECSIZE(fs) - 1) / REF_SECSIZE(fs);
        n_rsv = 1 + partition;
        n_dir = N_ROOTDIR * 32 / REF_SECSIZE(fs);
        break;
    case FS_FAT16:
        n_fat = ((n_clst * 2) + 4 + REF_SECSIZE(fs) - 1) / REF_SECSIZE(fs);
        n_rsv = 1 + partition;
        n_dir = N_ROOTDIR * 32 / REF_SECSIZE(fs);
        break;
    default:
        n_fat = ((n_clst * 4) + 8 + REF_SECSIZE(fs) - 1) / REF_SECSIZE(fs);
        n_rsv = 33 - partition;
        n_dir = 0;
    }
    b_fat = b_part + n_rsv;         /* FATs start sector */
    b_dir = b_fat + n_fat * N_FATS; /* Directory start sector */
    b_data = b_dir + n_dir;         /* Data start sector */

    /* Align data start sector to erase block boundary (for flash memory media) */
    if (fs->prt->ioctl(GET_BLOCK_SIZE, &n) != RES_OK) return FR_MKFS_ABORTED;
    n = (b_data + n - 1) & ~(n - 1);
    n_fat += (n - b_data) / N_FATS;
    /* b_dir and b_data are no longer used below */

    /* Determine number of cluster and final check of validity of the FAT type */
    n_clst = (n_part - n_rsv - n_fat * N_FATS - n_dir) / allocsize;
    if (   (fmt == FS_FAT16 && n_clst < 0xFF5)
        || (fmt == FS_FAT32 && n_clst < 0xFFF5))
        return FR_MKFS_ABORTED;

    /* Create partition table if needed */
    if (!partition) {
        DWORD n_disk = b_part + n_part;

        mem_set(fs->win, 0, REF_SECSIZE(fs));
        tbl = fs->win+MBR_Table;
        ST_DWORD(tbl, 0x00010180);      /* Partition start in CHS */
        if (n_disk < 63UL * 255 * 1024) {   /* Partition end in CHS */
            n_disk = n_disk / 63 / 255;
            tbl[7] = (BYTE)n_disk;
            tbl[6] = (BYTE)((n_disk >> 2) | 63);
        } else {
            ST_WORD(&tbl[6], 0xFFFF);
        }
        tbl[5] = 254;
        if (fmt != FS_FAT32)            /* System ID */
            tbl[4] = (n_part < 0x10000) ? 0x04 : 0x06;
        else
            tbl[4] = 0x0c;
        ST_DWORD(tbl+8, 63);            /* Partition start in LBA */
        ST_DWORD(tbl+12, n_part);       /* Partition size in LBA */
        ST_WORD(tbl+64, 0xAA55);        /* Signature */
        if (disk_write(drv, fs->win, 0, 1) != RES_OK)
            return FR_DISK_ERR;
        partition = 0xF8;
    } else {
        partition = 0xF0;
    }

    /* Create boot record */
    tbl = fs->win;                              /* Clear buffer */
    mem_set(tbl, 0, REF_SECSIZE(fs));
    ST_DWORD(tbl+BS_jmpBoot, 0x90FEEB);         /* Boot code (jmp $, nop) */
    ST_WORD(tbl+BPB_BytsPerSec, REF_SECSIZE(fs));        /* Sector size */
    tbl[BPB_SecPerClus] = (BYTE)allocsize;      /* Sectors per cluster */
    ST_WORD(tbl+BPB_RsvdSecCnt, n_rsv);         /* Reserved sectors */
    tbl[BPB_NumFATs] = N_FATS;                  /* Number of FATs */
    ST_WORD(tbl+BPB_RootEntCnt, REF_SECSIZE(fs) / 32 * n_dir); /* Number of rootdir entries */
    if (n_part < 0x10000) {                     /* Number of total sectors */
        ST_WORD(tbl+BPB_TotSec16, n_part);
    } else {
        ST_DWORD(tbl+BPB_TotSec32, n_part);
    }
    tbl[BPB_Media] = partition;                 /* Media descripter */
    ST_WORD(tbl+BPB_SecPerTrk, 63);             /* Number of sectors per track */
    ST_WORD(tbl+BPB_NumHeads, 255);             /* Number of heads */
    ST_DWORD(tbl+BPB_HiddSec, b_part);          /* Hidden sectors */
    n = get_fattime();                          /* Use current time as a VSN */
    if (fmt != FS_FAT32) {
        ST_DWORD(tbl+BS_VolID, n);              /* Volume serial number */
        ST_WORD(tbl+BPB_FATSz16, n_fat);        /* Number of secters per FAT */
        tbl[BS_DrvNum] = 0x80;                  /* Drive number */
        tbl[BS_BootSig] = 0x29;                 /* Extended boot signature */
        mem_cpy(tbl+BS_VolLab, "NO NAME    FAT     ", 19);  /* Volume lavel, FAT signature */
    } else {
        ST_DWORD(tbl+BS_VolID32, n);            /* Volume serial number */
        ST_DWORD(tbl+BPB_FATSz32, n_fat);       /* Number of secters per FAT */
        ST_DWORD(tbl+BPB_RootClus, 2);          /* Root directory cluster (2) */
        ST_WORD(tbl+BPB_FSInfo, 1);             /* FSInfo record offset (bs+1) */
        ST_WORD(tbl+BPB_BkBootSec, 6);          /* Backup boot record offset (bs+6) */
        tbl[BS_DrvNum32] = 0x80;                /* Drive number */
        tbl[BS_BootSig32] = 0x29;               /* Extended boot signature */
        mem_cpy(tbl+BS_VolLab32, "NO NAME    FAT32   ", 19);    /* Volume lavel, FAT signature */
    }
    ST_WORD(tbl+BS_55AA, 0xAA55);               /* Signature */
    if (REF_SECSIZE(fs) > 512U) {
        ST_WORD(tbl+REF_SECSIZE(fs)-2, 0xAA55);
    }
    if (disk_write(drv, tbl, b_part+0, 1) != RES_OK)
        return FR_DISK_ERR;
    if (fmt == FS_FAT32)
        disk_write(drv, tbl, b_part+6, 1);

    /* Initialize FAT area */
    for (m = 0; m < N_FATS; m++) {
        mem_set(tbl, 0, REF_SECSIZE(fs));        /* 1st sector of the FAT  */
        if (fmt != FS_FAT32) {
            n = (fmt == FS_FAT12) ? 0x00FFFF00 : 0xFFFFFF00;
            n |= partition;
            ST_DWORD(tbl, n);               /* Reserve cluster #0-1 (FAT12/16) */
        } else {
            ST_DWORD(tbl+0, 0xFFFFFFF8);    /* Reserve cluster #0-1 (FAT32) */
            ST_DWORD(tbl+4, 0xFFFFFFFF);
            ST_DWORD(tbl+8, 0x0FFFFFFF);    /* Reserve cluster #2 for root dir */
        }
        if (disk_write(drv, tbl, b_fat++, 1) != RES_OK)
            return FR_DISK_ERR;
        mem_set(tbl, 0, REF_SECSIZE(fs));        /* Following FAT entries are filled by zero */
        for (n = 1; n < n_fat; n++) {
            if (disk_write(drv, tbl, b_fat++, 1) != RES_OK)
                return FR_DISK_ERR;
        }
    }

    /* Initialize Root directory */
    m = (BYTE)((fmt == FS_FAT32) ? allocsize : n_dir);
    do {
        if (disk_write(drv, tbl, b_fat++, 1) != RES_OK)
            return FR_DISK_ERR;
    } while (--m);

    /* Create FSInfo record if needed */
    if (fmt == FS_FAT32) {
        ST_WORD(tbl+BS_55AA, 0xAA55);
        ST_DWORD(tbl+FSI_LeadSig, 0x41615252);
        ST_DWORD(tbl+FSI_StrucSig, 0x61417272);
        ST_DWORD(tbl+FSI_Free_Count, n_clst - 1);
        ST_DWORD(tbl+FSI_Nxt_Free, 0xFFFFFFFF);
        disk_write(drv, tbl, b_part+1, 1);
        disk_write(drv, tbl, b_part+7, 1);
    }

    return (fs->prt->ioctl(CTRL_SYNC, (void*)NULL) == RES_OK) ? FR_OK : FR_DISK_ERR;
}

#endif /* _USE_MKFS && !_FS_READONLY */
#endif /*0*/

/*-----------------------------------------------------------------------*/
/* Rename File/Directory                                                 */
/*-----------------------------------------------------------------------*/

FRESULT FATFS :: f_rename (
    FileInfo *info,  /* Existing file descriptor */
    char *new_name   /* Pointer to the new name */
)
{
	FRESULT res;
	FATDIR *new_dir = new FATDIR(this, info->dir_clust);
    if(!new_dir)
    	return FR_NO_MEMORY;

    res = chk_mounted(1);
    if (res != FR_OK) {
    	delete new_dir;
    	return res;
    }

    res = new_dir->follow_path(new_name, info->dir_clust);  /* Follow the file path, and set name in new_dir */
    if (res == FR_OK)
    	res = FR_EXIST;       				/* Any file or directory is already existing */
    if (res != FR_NO_FILE) {                /* Any error occured */
    	delete new_dir;
    	return res;
    }

    res = new_dir->dir_register();
    if(res != FR_OK) {
    	delete new_dir;
    	return res;
    }

    // dir points now to the new directory entry; copy stuff...
    new_dir->dir[FATDIR_Attr] = info->attrib;
	ST_WORD(new_dir->dir+FATDIR_WrtTime, info->time);   /* Created time */
	ST_WORD(new_dir->dir+FATDIR_WrtDate, info->date);   /* Created time */
	ST_WORD(new_dir->dir+FATDIR_FstClusLO, info->cluster);
	ST_WORD(new_dir->dir+FATDIR_FstClusHI, info->cluster >> 16);
	ST_DWORD(new_dir->dir+FATDIR_FileSize, info->size);
	wflag = 1;
    sync();

//	printf("New dir entry:\n");
//	new_dir->print_info();
	
    // now try to find and delete the old one.
    FATDIR *old_dir = new FATDIR(this, info->dir_clust);
    res = old_dir->dir_seek(info->dir_index);
    printf("SEEK on old dir, index = %d, resulted: %d\n", info->dir_index, res);
    if(res != FR_OK)
        goto exit_rename;

    res = old_dir->dir_find_lfn_start();
    printf("Now let's find the LFN... result = %d\n", res);
    if(res != FR_OK)
        goto exit_rename;
        

	res = old_dir->dir_remove();
    sync();

	// now set the info fields to point to new filename
	move_window(new_dir->sect);
	new_dir->get_fileinfo(info);

exit_rename:
    delete new_dir;
	delete old_dir;
    return res;
}


#ifndef BOOTLOADER
/* THE FOLLOWING FUNCTIONS ARE WRAPPERS TO MAKE THIS FILE SYSTEM INHERITABLE */
// functions for reading directories
// Opens directory (creates dir object, NULL = root)
Directory *FATFS::dir_open(FileInfo *f)  
{
//    f->print_info();
    FATDIR *fd = new FATDIR(this);
    Directory *d = new Directory(this, (DWORD)fd);
    
    FRESULT res = fd->open(f);
    if(res == FR_OK)
        return d;
    
    delete fd;
    delete d;
    return NULL;
}

// Closes (and destructs dir object)
void FATFS::dir_close(Directory *d)  
{
    FATDIR *fd = (FATDIR *)d->handle;
    delete fd;
    delete d;
}

// reads next entry from dir
FRESULT FATFS::dir_read(Directory *d, FileInfo *f)
{
    FATDIR *fd = (FATDIR *)d->handle;
    return fd->read_entry(f);
}

// creates directory according to file info
FRESULT FATFS::dir_create(FileInfo *f)
{
	FRESULT res;
	FATDIR *fd = new FATDIR(this, f->cluster);
    if(fd)
    	res = fd->mkdir(f->lfname);
    else
    	res = FR_NO_MEMORY;

    delete fd;
    return res;
}

// functions for reading and writing files
// Opens file (creates file object)
File *FATFS::file_open(FileInfo *info, BYTE flags)  
{
	FATFIL *ff = new FATFIL(this);
    File *f = new File(this, (DWORD)ff);
    FRESULT res = ff->open(info, flags);
    if(res == FR_OK) {
        return f;
    }
    delete ff;
    delete f;
    return NULL;    
}

// Closes file (and destructs file object)
void FATFS::file_close(File *f)                
{
    FATFIL *ff = (FATFIL *)f->handle;
    ff->close();
    delete ff;
    delete f;
}

FRESULT FATFS::file_read(File *f, void *buffer, DWORD len, UINT *bytes_read)
{
    FATFIL *ff = (FATFIL *)f->handle;
    return ff->read(buffer, len, bytes_read);
}

FRESULT FATFS::file_write(File *f, void *buffer, DWORD len, UINT *bytes_written)
{
    FATFIL *ff = (FATFIL *)f->handle;
//    printf("Writing %d bytes from %p...\n", len, buffer);
    FRESULT res = ff->write(buffer, len, bytes_written);
//    printf("... written: %d\n", *bytes_written);
    return res;
}

FRESULT FATFS::file_seek(File *f, DWORD pos)
{
    FATFIL *ff = (FATFIL *)f->handle;
    return ff->lseek(pos);
}

FRESULT FATFS::file_sync(File *f)
{
    FATFIL *ff = (FATFIL *)f->handle;
    return ff->sync();
}

FRESULT FATFS :: file_rename(FileInfo *fi, char *new_name)
{
    return f_rename(fi, new_name);
}

FRESULT FATFS :: file_delete(FileInfo *fi)
{
    FRESULT res;
    FATDIR dj(this), *sdj;

    res = chk_mounted(1);
    if (res != FR_OK)
		return res;

//	if (!(fi->cluster))
//		return FR_INVALID_NAME;				/* Is it the root directory? */
	
	if(fi->attrib & AM_RDO)					/* Is it read only? */
		return FR_DENIED;
	
	if(fi->attrib & AM_DIR) {				/* It is a sub-directory */
        if (fi->cluster < 2)
			return FR_INT_ERR;
			
		res = dj.open(fi);
		if (res != FR_OK)
			return res;
			
        res = dj.dir_seek(2);
        if (res != FR_OK)
			return res;
			
        res = dj.dir_read();
        if (res == FR_OK)
			return FR_DIR_NOT_EMPTY;  /* Not empty sub-dir */
			
        if (res != FR_NO_FILE)
			return res;
    }

    printf("fi->dir_clust = %d.\n", fi->dir_clust);
	sdj = new FATDIR(this, fi->dir_clust);
	if(!sdj)
		return FR_NO_MEMORY;
		
	res = sdj->dir_seek(fi->dir_index);
	if (res != FR_OK) {
		delete sdj;
		return res;
	}
    res = sdj->dir_find_lfn_start();
	if (res != FR_OK) {
		delete sdj;
		return res;
	}
    res = sdj->dir_remove();                  /* Remove directory entry */
    if (res == FR_OK) {
        if (fi->cluster)
            res = remove_chain(fi->cluster);   /* Remove the cluster chain */
        if (res == FR_OK)
			res = sync();
    }

	delete sdj;
    return res;
}

void FATFS::file_print_info(File *f)
{
	FATFIL *ff = (FATFIL *)f->handle;
	ff->print_info();
}
#endif
