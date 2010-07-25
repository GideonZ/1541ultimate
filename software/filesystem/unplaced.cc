/*-----------------------------------------------------------------------*/
/* Change Current Drive/Directory                                        */
/*-----------------------------------------------------------------------*/

#if _FS_RPATH

FRESULT f_chdir (
    const XCHAR *path   /* Pointer to the directory path */
)
{
    FRESULT res;
    DIR dj;
    NAMEBUF(sfn, lfn);
    BYTE *dir;


    res = chk_mounted(&path, &dj.fs, 0);
    if (res == FR_OK) {
        INITBUF(dj, sfn, lfn);
        res = follow_path(&dj, path);       /* Follow the file path */
        if (res == FR_OK) {                 /* Follow completed */
            dir = dj.dir;                   /* Pointer to the entry */
            if (!dir) {
                dj.fs->cdir = 0;            /* No entry (root dir) */
            } else {
                if (dir[DIR_Attr] & AM_DIR) /* Reached to the dir */
                    dj.fs->cdir = ((DWORD)LD_WORD(dir+DIR_FstClusHI) << 16) | LD_WORD(dir+DIR_FstClusLO);
                else
                    res = FR_NO_PATH;       /* Could not reach the dir (it is a file) */
            }
        }
        if (res == FR_NO_FILE) res = FR_NO_PATH;
    }

    LEAVE_FF(dj.fs, res);
}

#endif




#if _FS_MINIMIZE == 0
/*-----------------------------------------------------------------------*/
/* Get File Status                                                       */
/*-----------------------------------------------------------------------*/

FRESULT f_stat (
    const XCHAR *path,  /* Pointer to the file path */
    FILINFO *fno        /* Pointer to file information to return */
)
{
    FRESULT res;
    DIR dj;
    NAMEBUF(sfn, lfn);


    res = chk_mounted(&path, &dj.fs, 0);
    if (res == FR_OK) {
        INITBUF(dj, sfn, lfn);
        res = follow_path(&dj, path);   /* Follow the file path */
        if (res == FR_OK) {             /* Follwo completed */
            if (dj.dir) /* Found an object */
                get_fileinfo(&dj, fno);
            else        /* It is root dir */
                res = FR_INVALID_NAME;
        }
    }

    LEAVE_FF(dj.fs, res);
}


/*-----------------------------------------------------------------------*/
/* Delete a File or Directory                                            */
/*-----------------------------------------------------------------------*/

FRESULT f_unlink (
    const XCHAR *path       /* Pointer to the file or directory path */
)
{
    FRESULT res;
    DIR dj, sdj;
    NAMEBUF(sfn, lfn);
    BYTE *dir;
    DWORD dclst;


    res = chk_mounted(&path, &dj.fs, 1);
    if (res != FR_OK) LEAVE_FF(dj.fs, res);

    INITBUF(dj, sfn, lfn);
    res = follow_path(&dj, path);           /* Follow the file path */
    if (_FS_RPATH && res == FR_OK && (dj.fn[NS] & NS_DOT))
        res = FR_INVALID_NAME;
    if (res != FR_OK) LEAVE_FF(dj.fs, res); /* Follow failed */

    dir = dj.dir;
    if (!dir)                               /* Is it the root directory? */
        LEAVE_FF(dj.fs, FR_INVALID_NAME);
    if (dir[DIR_Attr] & AM_RDO)             /* Is it a R/O object? */
        LEAVE_FF(dj.fs, FR_DENIED);
    dclst = ((DWORD)LD_WORD(dir+DIR_FstClusHI) << 16) | LD_WORD(dir+DIR_FstClusLO);

    if (dir[DIR_Attr] & AM_DIR) {           /* It is a sub-directory */
        if (dclst < 2) LEAVE_FF(dj.fs, FR_INT_ERR);
        mem_cpy(&sdj, &dj, sizeof(DIR));    /* Check if the sub-dir is empty or not */
        sdj.sclust = dclst;
        res = dir_seek(&sdj, 2);
        if (res != FR_OK) LEAVE_FF(dj.fs, res);
        res = dir_read(&sdj);
        if (res == FR_OK) res = FR_DENIED;  /* Not empty sub-dir */
        if (res != FR_NO_FILE) LEAVE_FF(dj.fs, res);
    }

    res = dir_remove(&dj);                  /* Remove directory entry */
    if (res == FR_OK) {
        if (dclst)
            res = remove_chain(dj.fs, dclst);   /* Remove the cluster chain */
        if (res == FR_OK) res = sync(dj.fs);
    }

    LEAVE_FF(dj.fs, res);
}




/*-----------------------------------------------------------------------*/
/* Create a Directory                                                    */
/*-----------------------------------------------------------------------*/

FRESULT FATDIR :: mkdir (
    const XCHAR *newname       /* Pointer to the directory path */
)
{
    FRESULT res;
    NAMEBUF(sfn, lfn);
    BYTE *dir, n;
    DWORD dsect, dclst, pclst, tim;

    res = chk_mounted(1);
    if (res != FR_OK)
    	LEAVE_FF(fs, res);

    INITBUF(dj, sfn, lfn);
    res = follow_path(this, newname);       /* Follow the file path */
    if (res == FR_OK)
    	res = FR_EXIST;       				/* Any file or directory is already existing */
    if (res != FR_NO_FILE)                  /* Any error occured */
        LEAVE_FF(fs, res);

    dclst = create_chain(fs, 0);            /* Allocate a new cluster for new directory table */
    res = FR_OK;
    if (dclst == 0) res = FR_DENIED;
    if (dclst == 1) res = FR_INT_ERR;
    if (dclst == 0xFFFFFFFF) res = FR_DISK_ERR;
    if (res == FR_OK)
        res = fs->move_window(0);
    if (res != FR_OK)
    	LEAVE_FF(fs, res);

    dsect = fs->clust2sect(dclst);

    dir = fs->win;                          /* Initialize the new directory table */
    mem_set(dir, 0, REF_SECSIZE(dj.fs));
    mem_set(dir+DIR_Name, ' ', 8+3);        /* Create "." entry */
    dir[DIR_Name] = '.';
    dir[DIR_Attr] = AM_DIR;
    tim = get_fattime();
    ST_DWORD(dir+DIR_WrtTime, tim);
    ST_WORD(dir+DIR_FstClusLO, dclst);
    ST_WORD(dir+DIR_FstClusHI, dclst >> 16);
    mem_cpy(dir+32, dir, 32);           /* Create ".." entry */
    dir[33] = '.';

    pclst = sclust;
    if (fs->fs_type == FS_FAT32 && pclst == fs->dirbase)
        pclst = 0;

    ST_WORD(dir+32+DIR_FstClusLO, pclst);
    ST_WORD(dir+32+DIR_FstClusHI, pclst >> 16);

    for (n = 0; n < fs->csize; n++) {    /* Write dot entries and clear left sectors */
        fs->winsect = dsect++;
        fs->wflag = 1;
        res = fs->move_window(0);
        if (res)
			LEAVE_FF(fs, res);

        mem_set(dir, 0, REF_SECSIZE(fs));
    }

    res = dir_register(&dj);
    if (res != FR_OK) {
        remove_chain(dj.fs, dclst);
    } else {
        dir = dj.dir;
        dir[DIR_Attr] = AM_DIR;                 /* Attribute */
        ST_DWORD(dir+DIR_WrtTime, tim);         /* Crated time */
        ST_WORD(dir+DIR_FstClusLO, dclst);      /* Table start cluster */
        ST_WORD(dir+DIR_FstClusHI, dclst >> 16);
        dj.fs->wflag = 1;
        res = sync(dj.fs);
    }

    LEAVE_FF(dj.fs, res);
}




/*-----------------------------------------------------------------------*/
/* Change File Attribute                                                 */
/*-----------------------------------------------------------------------*/

FRESULT f_chmod (
    const XCHAR *path,  /* Pointer to the file path */
    BYTE value,         /* Attribute bits */
    BYTE mask           /* Attribute mask to change */
)
{
    FRESULT res;
    DIR dj;
    NAMEBUF(sfn, lfn);
    BYTE *dir;


    res = chk_mounted(&path, &dj.fs, 1);
    if (res == FR_OK) {
        INITBUF(dj, sfn, lfn);
        res = follow_path(&dj, path);       /* Follow the file path */
        if (_FS_RPATH && res == FR_OK && (dj.fn[NS] & NS_DOT))
            res = FR_INVALID_NAME;
        if (res == FR_OK) {
            dir = dj.dir;
            if (!dir) {                     /* Is it a root directory? */
                res = FR_INVALID_NAME;
            } else {                        /* File or sub directory */
                mask &= AM_RDO|AM_HID|AM_SYS|AM_ARC;    /* Valid attribute mask */
                dir[DIR_Attr] = (value & mask) | (dir[DIR_Attr] & (BYTE)~mask); /* Apply attribute change */
                dj.fs->wflag = 1;
                res = sync(dj.fs);
            }
        }
    }

    LEAVE_FF(dj.fs, res);
}




/*-----------------------------------------------------------------------*/
/* Change Timestamp                                                      */
/*-----------------------------------------------------------------------*/

FRESULT f_utime (
    const XCHAR *path,  /* Pointer to the file/directory name */
    const FILINFO *fno  /* Pointer to the timestamp to be set */
)
{
    FRESULT res;
    DIR dj;
    NAMEBUF(sfn, lfn);
    BYTE *dir;


    res = chk_mounted(&path, &dj.fs, 1);
    if (res == FR_OK) {
        INITBUF(dj, sfn, lfn);
        res = follow_path(&dj, path);   /* Follow the file path */
        if (_FS_RPATH && res == FR_OK && (dj.fn[NS] & NS_DOT))
            res = FR_INVALID_NAME;
        if (res == FR_OK) {
            dir = dj.dir;
            if (!dir) {             /* Root directory */
                res = FR_INVALID_NAME;
            } else {                /* File or sub-directory */
                ST_WORD(dir+DIR_WrtTime, fno->ftime);
                ST_WORD(dir+DIR_WrtDate, fno->fdate);
                dj.fs->wflag = 1;
                res = sync(dj.fs);
            }
        }
    }

    LEAVE_FF(dj.fs, res);
}




/*-----------------------------------------------------------------------*/
/* Rename File/Directory                                                 */
/*-----------------------------------------------------------------------*/

FRESULT f_rename (
    const XCHAR *path_old,  /* Pointer to the old name */
    const XCHAR *path_new   /* Pointer to the new name */
)
{
    FRESULT res;
    DIR dj_old, dj_new;
    NAMEBUF(sfn, lfn);
    BYTE buf[21], *dir;
    DWORD dw;


    INITBUF(dj_old, sfn, lfn);
    res = chk_mounted(&path_old, &dj_old.fs, 1);
    if (res == FR_OK) {
        dj_new.fs = dj_old.fs;
        res = follow_path(&dj_old, path_old);   /* Check old object */
        if (_FS_RPATH && res == FR_OK && (dj_old.fn[NS] & NS_DOT))
            res = FR_INVALID_NAME;
    }
    if (res != FR_OK) LEAVE_FF(dj_old.fs, res); /* The old object is not found */

    if (!dj_old.dir) LEAVE_FF(dj_old.fs, FR_NO_FILE);   /* Is root dir? */
    mem_cpy(buf, dj_old.dir+DIR_Attr, 21);      /* Save the object information */

    mem_cpy(&dj_new, &dj_old, sizeof(DIR));
    res = follow_path(&dj_new, path_new);       /* Check new object */
    if (res == FR_OK) res = FR_EXIST;           /* The new object name is already existing */
    if (res == FR_NO_FILE) {                    /* Is it a valid path and no name collision? */
        res = dir_register(&dj_new);            /* Register the new object */
        if (res == FR_OK) {
            dir = dj_new.dir;                   /* Copy object information into new entry */
            mem_cpy(dir+13, buf+2, 19);
            dir[DIR_Attr] = buf[0] | AM_ARC;
            dj_old.fs->wflag = 1;
            if (dir[DIR_Attr] & AM_DIR) {       /* Update .. entry in the directory if needed */
                dw = clust2sect(dj_new.fs, (DWORD)LD_WORD(dir+DIR_FstClusHI) | LD_WORD(dir+DIR_FstClusLO));
                if (!dw) {
                    res = FR_INT_ERR;
                } else {
                    res = move_window(dj_new.fs, dw);
                    dir = dj_new.fs->win+32;
                    if (res == FR_OK && dir[1] == '.') {
                        dw = (dj_new.fs->fs_type == FS_FAT32 && dj_new.sclust == dj_new.fs->dirbase) ? 0 : dj_new.sclust;
                        ST_WORD(dir+DIR_FstClusLO, dw);
                        ST_WORD(dir+DIR_FstClusHI, dw >> 16);
                        dj_new.fs->wflag = 1;
                    }
                }
            }
            if (res == FR_OK) {
                res = dir_remove(&dj_old);          /* Remove old entry */
                if (res == FR_OK)
                    res = sync(dj_old.fs);
            }
        }
    }

    LEAVE_FF(dj_old.fs, res);
}

#endif /* !_FS_READONLY */
#endif /* _FS_MINIMIZE == 0 */
#endif /* _FS_MINIMIZE <= 1 */
#endif /* _FS_MINIMIZE <= 2 */


#if _USE_MKFS && !_FS_READONLY
/*-----------------------------------------------------------------------*/
/* Create File System on the Drive                                       */
/*-----------------------------------------------------------------------*/
#define N_ROOTDIR   512         /* Multiple of 32 and <= 2048 */
#define N_FATS      1           /* 1 or 2 */
#define MAX_SECTOR  131072000UL /* Maximum partition size */
#define MIN_SECTOR  2000UL      /* Minimum partition size */


FRESULT f_mkfs (
    BYTE drv,           /* Logical drive number */
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
    if (drv >= _DRIVES) return FR_INVALID_DRIVE;
    if (partition >= 2) return FR_MKFS_ABORTED;

    /* Check mounted drive and clear work area */
    fs = FatFs[drv];
    if (!fs) return FR_NOT_ENABLED;
    fs->fs_type = 0;
    drv = LD2PD(drv);

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

