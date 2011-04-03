
#include "directory.h"
#include "fat_dir.h"
#include "rtc.h"
extern "C" {
	#include "dump_hex.h"
    #include "small_printf.h"
}
#include <string.h>

#define mem_set memset
#define mem_cmp memcmp
#define mem_cpy memcpy

/* Check if chr is contained in the string */
static
int chk_chr (const char* str, int chr) {
    while (*str && *str != chr) str++;
    return *str;
}

FATDIR::FATDIR(FATFS *filesys, DWORD start)
{
    fs = filesys;
    valid = 0;
#if _USE_LFN    /* LFN configuration */
    lfn = new WCHAR[256];
#endif
//    if(start) {
    	sclust = start;
    	if(dir_seek(0) == FR_OK) {
    		valid = 1;
    	}
//    }
}
 
FATDIR::~FATDIR()
{
#if _USE_LFN    /* LFN configuration */
    if(lfn)
        delete[] lfn; 
#endif
}
       
void FATDIR::print_info(void)
{
    printf("Index:          %d\n", index);
    printf("Start cluster:  %d\n", sclust);
    printf("Curr. cluster:  %d\n", clust);
    printf("Curr. sector:   %d\n", sect);
    printf("Dir Pntr:       %p\n", dir);
    printf("FN pointer:     %p\n", fn);
    printf("Valid           %d\n", valid);
#if _USE_LFN
    printf("LFN pointer:    %p\n", lfn);
    printf("LFN index:      %d\n", lfn_idx);
#endif
}
    

FRESULT FATDIR::validate(void)
{
    ENTER_FF(fs);

    if(!valid)
        return FR_NOT_READY;

    return fs->validate();
}

#if _FS_MINIMIZE <= 1
/*-----------------------------------------------------------------------*/
/* Create a Directroy Object                                             */
/*-----------------------------------------------------------------------*/
FRESULT FATDIR::open (
    XCHAR *path   /* Pointer to the directory path */
)
{
    FRESULT res;
    BYTE *dirbyte;

    res = fs->chk_mounted(0);
    if (res == FR_OK) {
        res = follow_path(path);            /* Follow the path to the directory */
        if (res == FR_OK) {                 /* Follow completed */
            dirbyte = dir;
            if (dirbyte) {                  /* It is not the root dir */
                if (dirbyte[FATDIR_Attr] & AM_DIR) {   /* The object is a directory */
                    sclust = ((DWORD)LD_WORD(dirbyte+FATDIR_FstClusHI) << 16) | LD_WORD(dirbyte+FATDIR_FstClusLO);
                } else {                    /* The object is not a directory */
                    res = FR_NO_PATH;
                }
            }
            if (res == FR_OK) {
                res = dir_seek(0);          /* Rewind dir */
            }
        }
        if (res == FR_NO_FILE) res = FR_NO_PATH;
    }

    if(res == FR_OK)
        valid = 1;

    LEAVE_FF(fs, res);
}

FRESULT FATDIR::open (FileInfo *fno)       /* Open the directory based on an entry in the dir above */
{
    FRESULT res;

    if(!fno) { // open root
        sclust = 0;
    } else {
        sclust = fno->cluster;
    }
    
    res = dir_seek(0);
    if(res == FR_OK)
        valid = 1;

    LEAVE_FF(fs, res);
}

/*-----------------------------------------------------------------------*/
/* Read Directory Entry in Sequense                                      */
/*-----------------------------------------------------------------------*/
FRESULT FATDIR::read_entry (
    FileInfo *fno        /* Pointer to file information to return */
)
{
    FRESULT res;

    res = validate();                       /* Check validity of the object */
    if (res == FR_OK) {
        if (!fno) {
            res = dir_seek(0);
        } else {
            res = dir_read();
            if (res == FR_NO_FILE) {
                sect = 0;
                res = FR_OK;
            }
            if (res == FR_OK) {         /* A valid entry is found */
                get_fileinfo(fno);      /* Get the object information */
                res = dir_next(false);  /* Increment index for next */
/*
                if (res == FR_NO_FILE) {
                    sect = 0;
                    res = FR_OK;
                }
*/
            }
        }
    }
    LEAVE_FF(fs, res);
}

#endif /* _FS_MINIMIZE <= 1 */



/*-----------------------------------------------------------------------*/
/* Directory handling - Seek directory index                             */
/*-----------------------------------------------------------------------*/
FRESULT FATDIR::dir_seek (
    WORD idx        /* Directory index number */
)
{
    DWORD clst;
    WORD ic;

    index = idx;
    clst = sclust;
    if (clst == 1 || clst >= fs->max_clust) /* Check start cluster range */
        return FR_INT_ERR;
    if (!clst && fs->fs_type == FS_FAT32)   /* Replace cluster# 0 with root cluster# if in FAT32 */
        clst = fs->dirbase;

    if (clst == 0) {    /* Static table */
        clust = clst;
        if (idx >= fs->n_rootdir) {      /* Index is out of range */
            return FR_INT_ERR;
        }
        sect = fs->dirbase + idx / (REF_SECSIZE(fs) / 32);  /* Sector# */
    }
    else {              /* Dynamic table */
        ic = REF_SECSIZE(fs) / 32 * fs->csize;  /* Entries per cluster */
        while (idx >= ic) { /* Follow cluster chain */
            clst = fs->get_fat(clst);               /* Get next cluster */
            if (clst == 0xFFFFFFFF) return FR_DISK_ERR; /* Disk error */
            if (clst < 2 || clst >= fs->max_clust)  /* Reached to end of table or int error */
                return FR_INT_ERR;
            idx -= ic;
        }
        clust = clst;
        sect = fs->clust2sect(clst) + idx / (REF_SECSIZE(fs) / 32); /* Sector# */
    }

    dir = fs->win + (idx % (REF_SECSIZE(fs) / 32)) * 32;    /* Ptr to the entry in the sector */
    fs->move_window(sect);
    return FR_OK;   /* Seek succeeded */
}


/*-----------------------------------------------------------------------*/
/* Directory handling - Move directory index next                        */
/*-----------------------------------------------------------------------*/
FRESULT FATDIR::dir_next ( /* FR_OK:Succeeded, FR_NO_FILE:End of table, FR_DENIED:EOT and could not stretch */
    bool stretch    /* FALSE: Do not stretch table, TRUE: stretch table if needed */
)
{
    DWORD clst;
    WORD i;

    i = index + 1;
    if (!i || !sect)    /* Report EOT when index has reached 65535 */
        return FR_NO_FILE;

    if (!(i % (REF_SECSIZE(fs) / 32))) {    /* Sector changed? */
        sect++;                 /* Next sector */

        if (clust == 0) {   /* Static table */
            if (i >= fs->n_rootdir) /* Report EOT when end of table */
                return FR_NO_FILE;
        }
        else {                  /* Dynamic table */
            if (((i / (REF_SECSIZE(fs) / 32)) & (fs->csize - 1)) == 0) {    /* Cluster changed? */
                clst = fs->get_fat(clust);              /* Get next cluster */
                if (clst <= 1) return FR_INT_ERR;
                if (clst == 0xFFFFFFFF) return FR_DISK_ERR;
                if (clst >= fs->max_clust) {                /* When it reached end of dynamic table */
#if !_FS_READONLY
                    BYTE c;
                    if (!stretch) return FR_NO_FILE;        /* When do not stretch, report EOT */
                    clst = fs->create_chain(clust);         /* stretch cluster chain */
                    if (clst == 0) return FR_DENIED;        /* No free cluster */
                    if (clst == 1) return FR_INT_ERR;
                    if (clst == 0xFFFFFFFF) return FR_DISK_ERR;
                    /* Clean-up stretched table */
                    if (fs->move_window(0)) return FR_DISK_ERR; /* Flush active window */
                    mem_set(fs->win, 0, REF_SECSIZE(fs));   /* Clear window buffer */
                    fs->winsect = fs->clust2sect(clst);     /* Cluster start sector */
                    for (c = 0; c < fs->csize; c++) {       /* Fill the new cluster with 0 */
                        fs->wflag = 1;
                        if (fs->move_window(0)) return FR_DISK_ERR;
                        fs->winsect++;
                    }
                    fs->winsect -= c;                       /* Rewind window address */
#else
                    return FR_NO_FILE;          /* Report EOT */
#endif
                }
                clust = clst;               /* Initialize data for new cluster */
                sect = fs->clust2sect(clst);
            }
        }
    }

    index = i;
    dir = fs->win + (i % (REF_SECSIZE(fs) / 32)) * 32;
    return FR_OK;
}



/*-----------------------------------------------------------------------*/
/* LFN handling - Test/Pick/Fit an LFN segment from/to directory entry   */
/*-----------------------------------------------------------------------*/
#if _USE_LFN
static
const BYTE LfnOfs[] = {1,3,5,7,9,14,16,18,20,22,24,28,30};  /* Offset of LFN chars in the directory entry */


static
BOOL cmp_lfn (          /* TRUE:Matched, FALSE:Not matched */
    WCHAR *lfnbuf,      /* Pointer to the LFN to be compared */
    BYTE *dir           /* Pointer to the directory entry containing a part of LFN */
)
{
    int i, s;
    WCHAR wc, uc;


    i = ((dir[LFATDIR_Ord] & 0xBF) - 1) * 13;  /* Get offset in the LFN buffer */
    s = 0; wc = 1;
    do {
        uc = LD_WORD(dir+LfnOfs[s]);    /* Pick an LFN character from the entry */
        if (wc) {   /* Last char has not been processed */
            wc = ff_wtoupper(uc);       /* Convert it to upper case */
            if (i >= _MAX_LFN || wc != ff_wtoupper(lfnbuf[i++]))    /* Compare it */
                return FALSE;           /* Not matched */
        } else {
            if (uc != 0xFFFF) return FALSE; /* Check filler */
        }
    } while (++s < 13);             /* Repeat until all chars in the entry are checked */

    if ((dir[LFATDIR_Ord] & 0x40) && wc && lfnbuf[i])  /* Last segment matched but different length */
        return FALSE;

    return TRUE;                    /* The part of LFN matched */
}



static
BOOL pick_lfn (         /* TRUE:Succeeded, FALSE:Buffer overflow */
    WCHAR *lfnbuf,      /* Pointer to the Unicode-LFN buffer */
    BYTE *dir           /* Pointer to the directory entry */
)
{
    int i, s;
    WCHAR wc, uc;


    i = ((dir[LFATDIR_Ord] & 0x3F) - 1) * 13;  /* Offset in the LFN buffer */

    s = 0; wc = 1;
    do {
        uc = LD_WORD(dir+LfnOfs[s]);            /* Pick an LFN character from the entry */
        if (wc) {   /* Last char has not been processed */
            if (i >= _MAX_LFN) return FALSE;    /* Buffer overflow? */
            lfnbuf[i++] = wc = uc;              /* Store it */
        } else {
            if (uc != 0xFFFF) return FALSE;     /* Check filler */
        }
    } while (++s < 13);                     /* Read all character in the entry */

    if (dir[LFATDIR_Ord] & 0x40) {             /* Put terminator if it is the last LFN part */
        if (i >= _MAX_LFN) return FALSE;    /* Buffer overflow? */
        lfnbuf[i] = 0;
    }

    return TRUE;
}


#if !_FS_READONLY
static
void fit_lfn (
    const WCHAR *lfnbuf,    /* Pointer to the LFN buffer */
    BYTE *dir,              /* Pointer to the directory entry */
    BYTE ord,               /* LFN order (1-20) */
    BYTE sum                /* SFN sum */
)
{
    int i, s;
    WCHAR wc;


    dir[LFATDIR_Chksum] = sum;         /* Set check sum */
    dir[LFATDIR_Attr] = AM_LFN;        /* Set attribute. LFN entry */
    dir[LFATDIR_Type] = 0;
    ST_WORD(dir+LFATDIR_FstClusLO, 0);

    i = (ord - 1) * 13;             /* Get offset in the LFN buffer */
    s = wc = 0;
    do {
        if (wc != 0xFFFF) wc = lfnbuf[i++]; /* Get an effective char */
        ST_WORD(dir+LfnOfs[s], wc); /* Put it */
        if (!wc) wc = 0xFFFF;       /* Padding chars following last char */
    } while (++s < 13);
    if (wc == 0xFFFF || !lfnbuf[i]) ord |= 0x40;    /* Bottom LFN part is the start of LFN sequence */
    dir[LFATDIR_Ord] = ord;            /* Set the LFN order */
}

#endif
#endif



/*-----------------------------------------------------------------------*/
/* Create numbered name                                                  */
/*-----------------------------------------------------------------------*/
#if _USE_LFN
void gen_numname (
    BYTE *dst,          /* Pointer to genartated SFN */
    const BYTE *src,    /* Pointer to source SFN to be modified */
    const WCHAR *lfn,   /* Pointer to LFN */
    WORD num            /* Sequense number */
)
{
    char ns[8];
    int i, j;


    mem_cpy(dst, src, 11);

    if (num > 5) {  /* On many collisions, generate a hash number instead of sequencial number */
        do num = (num >> 1) + (num << 15) + (WORD)*lfn++; while (*lfn);
    }

    /* itoa */
    i = 7;
    do {
        ns[i--] = (num % 10) + '0';
        num /= 10;
    } while (num);
    ns[i] = '~';

    /* Append the number */
    for (j = 0; j < i && dst[j] != ' '; j++) {
        if (IsDBCS1(dst[j])) {
            if (j == i - 1) break;
            j++;
        }
    }
    do {
        dst[j++] = (i < 8) ? ns[i++] : ' ';
    } while (j < 8);
}
#endif




/*-----------------------------------------------------------------------*/
/* Calculate sum of an SFN                                               */
/*-----------------------------------------------------------------------*/
#if _USE_LFN
static
BYTE sum_sfn (
    const BYTE *dir     /* Ptr to directory entry */
)
{
    BYTE sum = 0;
    int n = 11;

    do sum = (sum >> 1) + (sum << 7) + *dir++; while (--n);
    return sum;
}
#endif


#if _USE_LFN
FRESULT FATDIR::dir_find_lfn_start(void) // will set lfn_index correctly, based on current position
{
    if((dir[FATDIR_Attr] & AM_MASK) == AM_LFN)
        return FR_INT_ERR;

    BYTE sum = sum_sfn(dir);
    lfn_idx = 0xFFFF;
    WORD idx = index;
    WORD sfn_idx = index;
    FRESULT res;
    BYTE attr;
    
    while(--idx) {
        res = dir_seek(idx);
        if(res != FR_OK)
            return res;
        attr = dir[FATDIR_Attr] & AM_MASK;
        if(attr != AM_LFN)
            break; // no LFN
        if(dir[LFATDIR_Chksum] != sum)
            break; // no matching LFN
        if(dir[FATDIR_Name] & 0x40) {
            lfn_idx = idx;
            break;
        }
    }
    return dir_seek(sfn_idx);
}
#endif

/*-----------------------------------------------------------------------*/
/* Directory handling - Find an object in the directory                  */
/*-----------------------------------------------------------------------*/

FRESULT FATDIR::dir_find (void) /* link to file name in object */
{
    FRESULT res;
    BYTE c, *dirbyte;
#if _USE_LFN
    BYTE a, ord, sum;
#endif

    res = dir_seek(0);          /* Rewind directory object */
    if (res != FR_OK) return res;

#if _USE_LFN
    ord = sum = 0xFF;
#endif
    do {
        res = fs->move_window(sect);
        if (res != FR_OK) break;
        dirbyte = dir;                  /* Ptr to the directory entry of current index */
        c = dirbyte[FATDIR_Name];
        if (c == 0) { res = FR_NO_FILE; break; }    /* Reached to end of table */
#if _USE_LFN    /* LFN configuration */
        a = dirbyte[FATDIR_Attr] & AM_MASK;
        if (c == 0xE5 || ((a & AM_VOL) && a != AM_LFN)) {   /* An entry without valid data */
            ord = 0xFF;
        } else {
            if (a == AM_LFN) {          /* An LFN entry is found */
                if (lfn) {
                    if (c & 0x40) {     /* Is it start of LFN sequence? */
                        sum = dirbyte[LFATDIR_Chksum];
                        c &= 0xBF; ord = c; /* LFN start order */
                        lfn_idx = index;
                    }
                    /* Check validity of the LFN entry and compare it with given name */
                    ord = (c == ord && sum == dirbyte[LFATDIR_Chksum] && cmp_lfn(lfn, dirbyte)) ? ord - 1 : 0xFF;
                }
            } else {                    /* An SFN entry is found */
                if (!ord && sum == sum_sfn(dirbyte)) break; /* LFN matched? */
                ord = 0xFF; lfn_idx = 0xFFFF;   /* Reset LFN sequence */
                if (!(fn[NS] & NS_LOSS) && !mem_cmp(dirbyte, fn, 11)) break;    /* SFN matched? */
            }
        }
#else       /* Non LFN configuration */
        if (!(dirbyte[FATDIR_Attr] & AM_VOL) && !mem_cmp(dirbyte, fn, 11)) /* Is it a valid entry? */
            break;
#endif
        res = dir_next(false);      /* Next entry */
    } while (res == FR_OK);

    return res;
}




/*-----------------------------------------------------------------------*/
/* Read an object from the directory                                     */
/*-----------------------------------------------------------------------*/
#if _FS_MINIMIZE <= 1
FRESULT FATDIR::dir_read (void) /* Read entry that the object describes/points to */
{
    FRESULT res;
    BYTE c, *dirbyte;
#if _USE_LFN
    BYTE a, ord = 0xFF, sum = 0xFF;
#endif

    res = FR_NO_FILE;
    while (sect) {
        res = fs->move_window(sect);
        if (res != FR_OK) break;
        dirbyte = dir;                  /* Ptr to the directory entry of current index */
        c = dirbyte[FATDIR_Name];
        if (c == 0) { res = FR_NO_FILE; break; }    /* Reached to end of table */
#if _USE_LFN    /* LFN configuration */
        a = dirbyte[FATDIR_Attr] & AM_MASK;
        if (c == 0xE5 || (!_FS_RPATH && c == '.') || ((a & AM_VOL) && a != AM_LFN)) {   /* An entry without valid data */
            ord = 0xFF;
        } else {
            if (a == AM_LFN) {          /* An LFN entry is found */
                if (c & 0x40) {         /* Is it start of LFN sequence? */
                    sum = dirbyte[LFATDIR_Chksum];
                    c &= 0xBF; ord = c;
                    lfn_idx = index;
                }
                /* Check LFN validity and capture it */
                ord = (c == ord && sum == dirbyte[LFATDIR_Chksum] && pick_lfn(lfn, dirbyte)) ? ord - 1 : 0xFF;
            } else {                    /* An SFN entry is found */
                if (ord || sum != sum_sfn(dirbyte)) /* Is there a valid LFN? */
                    lfn_idx = 0xFFFF;       /* It has no LFN. */
                break;
            }
        }
#else       /* Non LFN configuration */
        if (c != 0xE5 && (_FS_RPATH || c != '.') && !(dirbyte[FATDIR_Attr] & AM_VOL))  /* Is it a valid entry? */
            break;
#endif
        res = dir_next(false);              /* Next entry */
        if (res != FR_OK) break;
    }

    if (res != FR_OK) sect = 0;

    return res;
}
#endif



/*-----------------------------------------------------------------------*/
/* Register an object to the directory                                   */
/*-----------------------------------------------------------------------*/
#if !_FS_READONLY
/* FR_OK:Successful, FR_DENIED:No free entry or too many SFN collision, FR_DISK_ERR:Disk error */
FRESULT FATDIR::dir_register (void)    /* Objects points to directory with object name to be created */
{
    FRESULT res;
    BYTE c, *dirbyte;
#if _USE_LFN    /* LFN configuration */
    WORD n, ne, is = 0xFFFF;
    BYTE tmp_sn[12], *tmp_fn, sum;
    WCHAR *tmp_lfn;

    tmp_fn  = fn;
    tmp_lfn = lfn;

    printf("Register %s\n", fn);

    mem_cpy(tmp_sn, tmp_fn, 12);

    if (_FS_RPATH && (tmp_sn[NS] & NS_DOT))
        return FR_INVALID_NAME; /* Cannot create dot entry */

    if (tmp_sn[NS] & NS_LOSS) {         /* When LFN is out of 8.3 format, generate a numbered name */
        tmp_fn[NS] = 0; lfn = NULL;         /* Find only SFN */
        for (n = 1; n < 100; n++) {
            gen_numname(tmp_fn, tmp_sn, tmp_lfn, n);    /* Generate a numbered name */
            res = dir_find();               /* Check if the name collides with existing SFN */
            if (res != FR_OK) break;
        }
        if (n == 100) return FR_DENIED;     /* Abort if too many collisions */
        if (res != FR_NO_FILE) return res;  /* Abort if the result is other than 'not collided' */
        tmp_fn[NS] = tmp_sn[NS]; lfn = tmp_lfn;
    }

    if (tmp_sn[NS] & NS_LFN) {          /* When LFN is to be created, reserve reserve an SFN + LFN entries. */
        for (ne = 0; tmp_lfn[ne]; ne++) ;
        ne = (ne + 25) / 13;
    } else {                        /* Otherwise reserve only an SFN entry. */
        ne = 1;
    }

    printf("Number of entries to reserve: %d\n", ne);

    /* Reserve contiguous entries */
    res = dir_seek(0);
    if (res != FR_OK) return res;
    n = is = 0;
    do {
        res = fs->move_window(sect);
        if (res != FR_OK) break;
        c = *dir;               /* Check the entry status */
        if (c == 0xE5 || c == 0) {  /* Is it a blank entry? */
            if (n == 0) is = index; /* First index of the contiguous entry */
            if (++n == ne) break;   /* A contiguous entry that required count is found */
        } else {
            n = 0;                  /* Not a blank entry. Restart to search */
        }
        res = dir_next(true);   /* Next entry with table stretch */
    } while (res == FR_OK);

    if (res == FR_OK && ne > 1) {   /* Initialize LFN entry if needed */
        res = dir_seek(is);
        if (res == FR_OK) {
            sum = sum_sfn(fn);  /* Sum of the SFN tied to the LFN */
            ne--;
            do {                    /* Store LFN entries in bottom first */
                res = fs->move_window(sect);
                if (res != FR_OK) break;
                fit_lfn(lfn, dir, (BYTE)ne, sum);
                fs->wflag = 1;
                res = dir_next(false);  /* Next entry */
            } while (res == FR_OK && --ne);
        }
    }

#else   /* Non LFN configuration */
    res = dir_seek(0);
    if (res == FR_OK) {
        do {    /* Find a blank entry for the SFN */
            res = fs->move_window(sect);
            if (res != FR_OK) break;
            c = *dir;
            if (c == 0xE5 || c == 0) break; /* Is it a blank entry? */
            res = dir_next(true);       /* Next entry with table stretch */
        } while (res == FR_OK);
    }
#endif

    if (res == FR_OK) {     /* Initialize the SFN entry */
        res = fs->move_window(sect);
        if (res == FR_OK) {
            dirbyte = dir;
            mem_set(dirbyte, 0, 32);        /* Clean the entry */
            mem_cpy(dirbyte, fn, 11);   /* Put SFN */
            dirbyte[FATDIR_NTres] = *(fn+NS) & (NS_BODY | NS_EXT); /* Put NT flag */
            fs->wflag = 1;
        }
    }

	lfn_idx = is;
//    printf("Dir = %p. Win = %p. Dumping:", dir, fs->win);
//    dump_hex(fs->win, 512);

    return res;
}
#endif /* !_FS_READONLY */


/*-----------------------------------------------------------------------*/
/* Remove an object from the directory                                   */
/*-----------------------------------------------------------------------*/
#if !_FS_READONLY && !_FS_MINIMIZE
/* FR_OK: Successful, FR_DISK_ERR: A disk error */
FRESULT FATDIR::dir_remove (void)  /* Directory object pointing the entry to be removed */
{
    FRESULT res;
#if _USE_LFN    /* LFN configuration */
    WORD i;

    i = index;  /* SFN index */
    res = dir_seek((WORD)((lfn_idx == 0xFFFF) ? i : lfn_idx));  /* Goto the SFN or top of the LFN entries */
	
    if (res == FR_OK) {
        do {
            res = fs->move_window(sect);
            if (res != FR_OK) break;
            *dir = 0xE5;            /* Mark the entry "deleted" */
            fs->wflag = 1;
            if (index >= i) break;  /* When reached SFN, all entries of the object has been deleted. */
            res = dir_next(false);  /* Next entry */
        } while (res == FR_OK);
        if (res == FR_NO_FILE) res = FR_INT_ERR;
    }
	
#else           /* Non LFN configuration */
    res = dir_seek(index);
    if (res == FR_OK) {
        res = fs->move_window(sect);
        if (res == FR_OK) {
            *dir = 0xE5;            /* Mark the entry "deleted" */
            fs->wflag = 1;
        }
    }
#endif

    return res;
}
#endif /* !_FS_READONLY */


/*-----------------------------------------------------------------------*/
/* Pick a segment and create the object name in directory form           */
/*-----------------------------------------------------------------------*/
FRESULT FATDIR::create_name (
    XCHAR **path  /* Pointer to pointer to the segment in the path string */
)
{
#ifdef _EXCVT
    static const BYTE cvt[] = _EXCVT;
#endif

#if _USE_LFN    /* LFN configuration */
    BYTE b, cf;
    WCHAR w, *tmp_lfn;
    int i, ni, si, di;
    XCHAR *p;

    /* Create LFN in Unicode */
    si = di = 0;
    p = *path;
    tmp_lfn = lfn;
    for (;;) {
        w = p[si++];                    /* Get a character */
        if (w < ' ' || w == '/' || w == '\\') break;    /* Break on end of segment */
        if (di >= _MAX_LFN)             /* Reject too long name */
            return FR_INVALID_NAME;
#if !_LFN_UNICODE
        w &= 0xFF;
        if (IsDBCS1(w)) {               /* If it is a DBC 1st byte */
            b = p[si++];                /* Get 2nd byte */
            if (!IsDBCS2(b))            /* Reject invalid code for DBC */
                return FR_INVALID_NAME;
            w = (w << 8) + b;
        }
        w = ff_convert(w, 1);           /* Convert OEM to Unicode */
        if (!w) return FR_INVALID_NAME; /* Reject invalid code */
#endif
        if (w < 0x80 && chk_chr("\"*:<>\?|\x7F", w)) /* Reject illegal chars for LFN */
            return FR_INVALID_NAME;
        tmp_lfn[di++] = w;                  /* Store the Unicode char */
    }
    *path = &p[si];                     /* Rerurn pointer to the next segment */
    cf = (w < ' ') ? NS_LAST : 0;       /* Set last segment flag if end of path */
#if _FS_RPATH
    if ((di == 1 && tmp_lfn[di - 1] == '.') || /* Is this a dot entry? */
        (di == 2 && tmp_lfn[di - 1] == '.' && tmp_lfn[di - 2] == '.')) {
        tmp_lfn[di] = 0;
        for (i = 0; i < 11; i++)
            fn[i] = (i < di) ? '.' : ' ';
        fn[i] = cf | NS_DOT;        /* This is a dot entry */
        return FR_OK;
    }
#endif
    while (di) {                        /* Strip trailing spaces and dots */
        w = tmp_lfn[di - 1];
        if (w != ' ' && w != '.') break;
        di--;
    }
    if (!di) return FR_INVALID_NAME;    /* Reject null string */

    tmp_lfn[di] = 0;                        /* LFN is created */
//###
    /* Create SFN in directory form */
    mem_set(fn, ' ', 11);
    for (si = 0; tmp_lfn[si] == ' ' || tmp_lfn[si] == '.'; si++) ;  /* Strip leading spaces and dots */
    if (si) cf |= NS_LOSS | NS_LFN;
    while (di && tmp_lfn[di - 1] != '.') di--;  /* Find extension (di<=si: no extension) */

    b = i = 0; ni = 8;
    for (;;) {
        w = tmp_lfn[si++];                  /* Get an LFN char */
        if (!w) break;                  /* Break on enf of the LFN */
        if (w == ' ' || (w == '.' && si != di)) {   /* Remove spaces and dots */
            cf |= NS_LOSS | NS_LFN; continue;
        }

        if (i >= ni || si == di) {      /* Extension or end of SFN */
            if (ni == 11) {             /* Long extension */
                cf |= NS_LOSS | NS_LFN; break;
            }
            if (si != di) cf |= NS_LOSS | NS_LFN;   /* Out of 8.3 format */
            if (si > di) break;         /* No extension */
            si = di; i = 8; ni = 11;    /* Enter extension section */
            b <<= 2; continue;
        }

        if (w >= 0x80) {                /* Non ASCII char */
#ifdef _EXCVT
            w = ff_convert(w, 0);       /* Unicode -> OEM code */
            if (w) w = cvt[w - 0x80];   /* Convert extended char to upper (SBCS) */
#else
            w = ff_convert(ff_wtoupper(w), 0);  /* Upper converted Unicode -> OEM code */
#endif
            cf |= NS_LFN;               /* Force create LFN entry */
        }

        if (_DF1S && w >= 0x100) {      /* Double byte char */
            if (i >= ni - 1) {
                cf |= NS_LOSS | NS_LFN; i = ni; continue;
            }
            fn[i++] = (BYTE)(w >> 8);
        } else {                        /* Single byte char */
            if (!w || chk_chr("+,;[=]", w)) {       /* Replace illegal chars for SFN */
                w = '_'; cf |= NS_LOSS | NS_LFN;    /* Lossy conversion */
            } else {
                if (IsUpper(w)) {       /* ASCII large capital */
                    b |= 2;
                } else {
                    if (IsLower(w)) {   /* ASCII small capital */
                        b |= 1; w -= 0x20;
                    }
                }
            }
        }
        fn[i++] = (BYTE)w;
    }

    if (fn[0] == 0xE5) fn[0] = 0x05;    /* If the first char collides with deleted mark, replace it with 0x05 */

    if (ni == 8) b <<= 2;
    if ((b & 0x0C) == 0x0C || (b & 0x03) == 0x03)   /* Create LFN entry when there are composite capitals */
        cf |= NS_LFN;
    if (!(cf & NS_LFN)) {                       /* When LFN is in 8.3 format without extended char, NT flags are created */
        if ((b & 0x03) == 0x01) cf |= NS_EXT;   /* NT flag (Extension has only small capital) */
        if ((b & 0x0C) == 0x04) cf |= NS_BODY;  /* NT flag (Filename has only small capital) */
    }

    fn[NS] = cf;    /* SFN is created */

    return FR_OK;


#else   /* Non-LFN configuration */
    BYTE b, c, d, *tmp_sfn;
    int ni, si, i;
    char *p;

    /* Create file name in directory form */
    tmp_sfn = fn;
    mem_set(tmp_sfn, ' ', 11);
    si = i = b = 0; ni = 8;
    p = *path;
#if _FS_RPATH
    if (p[si] == '.') { /* Is this a dot entry? */
        for (;;) {
            c = p[si++];
            if (c != '.' || si >= 3) break;
            tmp_sfn[i++] = c;
        }
        if (c != '/' && c != '\\' && c > ' ') return FR_INVALID_NAME;
        *path = &p[si];                                 /* Rerurn pointer to the next segment */
        tmp_sfn[NS] = (c <= ' ') ? NS_LAST | NS_DOT : NS_DOT;   /* Set last segment flag if end of path */
        return FR_OK;
    }
#endif
    for (;;) {
        c = p[si++];
        if (c <= ' ' || c == '/' || c == '\\') break;   /* Break on end of segment */
        if (c == '.' || i >= ni) {
            if (ni != 8 || c != '.') return FR_INVALID_NAME;
            i = 8; ni = 11;
            b <<= 2; continue;
        }
        if (c >= 0x80) {                /* Extended char */
#ifdef _EXCVT
            c = cvt[c - 0x80];          /* Convert extend char (SBCS) */
#else
            b |= 3;                     /* Eliminate NT flag if ext char is exist */
#if !_DF1S  /* ASCII only cfg */
            return FR_INVALID_NAME;
#endif
#endif
        }
        if (IsDBCS1(c)) {               /* DBC 1st byte? */
            d = p[si++];                /* Get 2nd byte */
            if (!IsDBCS2(d) || i >= ni - 1) /* Reject invalid DBC */
                return FR_INVALID_NAME;
            tmp_sfn[i++] = c;
            tmp_sfn[i++] = d;
        } else {                        /* Single byte code */
            if (chk_chr(" \"*+,[=]|\x7F", c))   /* Reject illegal chrs for SFN */
                return FR_INVALID_NAME;
            if (IsUpper(c)) {           /* ASCII large capital? */
                b |= 2;
            } else {
                if (IsLower(c)) {       /* ASCII small capital? */
                    b |= 1; c -= 0x20;
                }
            }
            tmp_sfn[i++] = c;
        }
    }
    *path = &p[si];                     /* Rerurn pointer to the next segment */
    c = (c <= ' ') ? NS_LAST : 0;       /* Set last segment flag if end of path */

    if (!i) return FR_INVALID_NAME;     /* Reject null string */
    if (tmp_sfn[0] == 0xE5) tmp_sfn[0] = 0x05;  /* When first char collides with 0xE5, replace it with 0x05 */

    if (ni == 8) b <<= 2;
    if ((b & 0x03) == 0x01) c |= NS_EXT;    /* NT flag (Extension has only small capital) */
    if ((b & 0x0C) == 0x04) c |= NS_BODY;   /* NT flag (Filename has only small capital) */

    tmp_sfn[NS] = c;        /* Store NT flag, File name is created */

    return FR_OK;
#endif
}


/*-----------------------------------------------------------------------*/
/* Get file information from directory entry                             */
/*-----------------------------------------------------------------------*/
#if _FS_MINIMIZE <= 1
void FATDIR::get_fileinfo( /* No return code */
    FileInfo *fno        /* Pointer to the file information to be filled */
)
{
    int i;
    BYTE c, nt, *dirbyte;
    char *p;

    fno->fs = fs;

//    p = fno->short_name;
    if (sect) {
        dirbyte = dir;
/*
        nt = dirbyte[FATDIR_NTres];        // NT flag
        for (i = 0; i < 8; i++) {   // Copy name body
            c = dirbyte[i];
            if (c == ' ') break;
            if (c == 0x05) c = 0xE5;
            if (_USE_LFN && (nt & NS_BODY) && IsUpper(c)) c += 0x20;
            *p++ = c;
        }
        if (dirbyte[8] != ' ') {        // Copy name extension
            *p++ = '.';
            for (i = 8; i < 11; i++) {
                c = dirbyte[i];
                if (c == ' ') break;
                if (_USE_LFN && (nt & NS_EXT) && IsUpper(c)) c += 0x20;
                *p++ = c;
            }
        }
*/
        fno->dir_clust = this->sclust;
//        fno->dir_offset = (WORD)(dir - fs->win);
//        fno->dir_sector = sect;
//        fno->lfn_index = lfn_idx;
        fno->dir_index = index;
        fno->attrib  = dirbyte[FATDIR_Attr];               /* Attribute */
        fno->size    = LD_DWORD(dirbyte+FATDIR_FileSize);    /* Size */
        fno->date    = LD_WORD(dirbyte+FATDIR_WrtDate);      /* Date */
        fno->time    = LD_WORD(dirbyte+FATDIR_WrtTime);      /* Time */
        fno->cluster = ((DWORD)LD_WORD(dirbyte+FATDIR_FstClusHI) << 16) |
                               LD_WORD(dirbyte+FATDIR_FstClusLO);
        
        p = fno->extension;
        for (i = 8; i < 11; i++) {
            c = dirbyte[i];
            if (c == ' ') {
                break;
            }
            if (IsLower(c))
                c -= 0x20;
            *p++ = c;
        }
        *p = '\0';
    }

#if _USE_LFN
    if (fno->lfname) {
        XCHAR *tp = fno->lfname;
        WCHAR w, *lfn;
        char *b;
        
        i = 0;
        if (sect && lfn_idx != 0xFFFF) {/* Get LFN if available */
            lfn = this->lfn;
            while ((w = *lfn++) != 0) {         /* Get an LFN char */
#if !_LFN_UNICODE
                w = ff_convert(w, 0);           /* Unicode -> OEM conversion */
                if (!w) {                       /* Could not convert, no LFN */
                    i = 0;
                    break;
                }
                if (_DF1S && w >= 0x100)        /* Put 1st byte if it is a DBC */
                    tp[i++] = (XCHAR)(w >> 8);
#endif
                if (i >= fno->lfsize - 1) {
                    //i = 0; removed to truncate, instead of drop
                    break;
                } /* Buffer overrun, no LFN */
                tp[i++] = (XCHAR)w;
            }
            tp[i] = 0;  /* Terminator */
        } else { /* No LFN, so copy short name info lfn space */
#if !_LFN_UNICODE
/*
            b = fno->short_name;
            while((*b)&&(i<13)) {
                tp[i++] = (XCHAR) *(b++);
            }
*/
            p = fno->lfname;

            nt = dirbyte[FATDIR_NTres];        // NT flag
            for (i = 0; i < 8; i++) {   // Copy name body
                c = dirbyte[i];
                if (c == ' ') break;
                if (c == 0x05) c = 0xE5;
                if (_USE_LFN && (nt & NS_BODY) && IsUpper(c)) c += 0x20;
                *p++ = c;
            }
            if (dirbyte[8] != ' ') {        // Copy name extension
                *p++ = '.';
                for (i = 8; i < 11; i++) {
                    c = dirbyte[i];
                    if (c == ' ') break;
                    if (_USE_LFN && (nt & NS_EXT) && IsUpper(c)) c += 0x20;
                    *p++ = c;
                }
            }
            *p = '\0';
#endif
        }
    }     
#endif
}
#endif /* _FS_MINIMIZE <= 1 */


/*-----------------------------------------------------------------------*/
/* Follow a file path                                                    */
/*-----------------------------------------------------------------------*/
FRESULT FATDIR::follow_path (  /* FR_OK(0): successful, !=0: error code */
    XCHAR *path,   /* Full-path string to find a file or directory */
    DWORD start_clust
)
{
    FRESULT res;
    BYTE *dirbyte, last;

    while (!_USE_LFN && *path == ' ') path++;   /* Skip leading spaces */
#if _FS_RPATH
    if (*path == '/' || *path == '\\') { /* There is a heading separator */
        path++; sclust = 0;     /* Strip it and start from the root dir */
    } else {                            /* No heading saparator */
        sclust = fs->cdir;  /* Start from the current dir */
    }
#else
    if (*path == '/' || *path == '\\')  /* Strip heading separator if exist */
        path++;
    sclust = start_clust;               /* Start from the root dir (if no parameter is given) */
#endif

    if ((UINT)*path < ' ') {            /* Null path means the start directory itself */
        res = dir_seek(0);
        dir = NULL;

    } else {                            /* Follow path */
        for (;;) {
            res = create_name(&path);   /* Get a segment */
            if (res != FR_OK) break;
            res = dir_find();             /* Find it */
            last = *(fn+NS) & NS_LAST;
            if (res != FR_OK) {             /* Could not find the object */
                if (res == FR_NO_FILE && !last)
                    res = FR_NO_PATH;
                break;
            }
            if (last) break;                /* Last segment match. Function completed. */
            dirbyte = dir;                  /* There is next segment. Follow the sub directory */
            if (!(dirbyte[FATDIR_Attr] & AM_DIR)) { /* Cannot follow because it is a file */
                res = FR_NO_PATH; break;
            }
            sclust = ((DWORD)LD_WORD(dirbyte+FATDIR_FstClusHI) << 16) | LD_WORD(dirbyte+FATDIR_FstClusLO);
        }
    }

    return res;
}

/*-----------------------------------------------------------------------*/
/* Create a Directory                                                    */
/*-----------------------------------------------------------------------*/

FRESULT FATDIR :: mkdir (XCHAR *newname)       /* Pointer to the directory path */
{
    FRESULT res;
    BYTE *dir_byte, n;
    DWORD dsect, dclst, pclst, tim;

    res = fs->chk_mounted(1);
    if (res != FR_OK)
    	LEAVE_FF(fs, res);

//    INITBUF(sfn, lfn);
    res = follow_path(newname, sclust);     /* Follow the file path */
    if (res == FR_OK)
    	res = FR_EXIST;       				/* Any file or directory is already existing */
    if (res != FR_NO_FILE)                  /* Any error occured */
        LEAVE_FF(fs, res);

    dclst = fs->create_chain(0);            /* Allocate a new cluster for new directory table */
    res = FR_OK;
    if (dclst == 0) res = FR_DENIED;
    if (dclst == 1) res = FR_INT_ERR;
    if (dclst == 0xFFFFFFFF) res = FR_DISK_ERR;
    if (res == FR_OK)
        res = fs->move_window(0);
    if (res != FR_OK)
    	LEAVE_FF(fs, res);

    dsect = fs->clust2sect(dclst);

    dir_byte = fs->win;                          /* Initialize the new directory table */
    mem_set(dir_byte, 0, REF_SECSIZE(fs));
    mem_set(dir_byte+FATDIR_Name, ' ', 8+3);        /* Create "." entry */
    dir_byte[FATDIR_Name] = '.';
    dir_byte[FATDIR_Attr] = AM_DIR;
    tim = rtc.get_fat_time();
    ST_DWORD(dir_byte+FATDIR_WrtTime, tim);
    ST_WORD(dir_byte+FATDIR_FstClusLO, dclst);
    ST_WORD(dir_byte+FATDIR_FstClusHI, dclst >> 16);
    mem_cpy(dir_byte+32, dir_byte, 32);           /* Create ".." entry */
    dir_byte[33] = '.';

    pclst = sclust;
    if (fs->fs_type == FS_FAT32 && pclst == fs->dirbase)
        pclst = 0;

    ST_WORD(dir_byte+32+FATDIR_FstClusLO, pclst);
    ST_WORD(dir_byte+32+FATDIR_FstClusHI, pclst >> 16);

    for (n = 0; n < fs->csize; n++) {    /* Write dot entries and clear left sectors */
        fs->winsect = dsect++;
        fs->wflag = 1;
        res = fs->move_window(0);
        if (res)
			LEAVE_FF(fs, res);

        mem_set(dir_byte, 0, REF_SECSIZE(fs));
    }

    res = dir_register();
    // dir points now to the new directory entry
    if (res != FR_OK) {
        fs->remove_chain(dclst);
    } else {
        dir[FATDIR_Attr] = AM_DIR;                 /* Attribute */
        ST_DWORD(dir+FATDIR_WrtTime, tim);         /* Crated time */
        ST_WORD(dir+FATDIR_FstClusLO, dclst);      /* Table start cluster */
        ST_WORD(dir+FATDIR_FstClusHI, dclst >> 16);
        fs->wflag = 1;
        res = fs->sync();
    }

    LEAVE_FF(fs, res);
}

