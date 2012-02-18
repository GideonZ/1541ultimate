#ifndef FATFILE_H
#define FATFILE_H

#include "fat_fs.h"
#include "fat_dir.h"

/* File object structure */
class FATFIL { /* this is a FAT file.. and should be derived from generic FILE class. */
private:
    FATFS*  fs;         /* Pointer to the owner file system object */
    BYTE    valid;      /* Set to a non-zero value when file is successfully opened */
    BYTE    flag;       /* File status flags */
    BYTE    csect;      /* Sector address in the cluster */
    DWORD   fptr;       /* File R/W pointer */
    DWORD   fsize;      /* File size */
    DWORD   org_clust;  /* File start cluster */
    DWORD   curr_clust; /* Current cluster */
    DWORD   dsect;      /* Current data sector */
#if !_FS_READONLY
    FATDIR *dir_obj;    /* Information about the directory entry that points to this file */
#endif
#if !_FS_TINY
    BYTE    buf[_MAX_SS];/* File R/W buffer */
#endif
public:
    FATFIL(FATFS *);                /* constructor */
    FATFIL(FATFS *, XCHAR *, BYTE); /* constructor, calls open, too */
    ~FATFIL();                      /* destructor */

    void print_info(void);			// prints all variables for debug
    FRESULT validate(void);                    /* Check if everything is still ok */
    FRESULT open(XCHAR*, DWORD s, BYTE, WORD *dir_index);  /* Open or create a file */
    FRESULT open(FileInfo *fi, BYTE);          /* Open or create a file, using a known dir entry */
    FRESULT read(void*, UINT, UINT*);          /* Read data from a file */
    FRESULT write(const void*, UINT, UINT*);   /* Write data to a file */
    FRESULT lseek(DWORD);                      /* Move file pointer of a file object */
    FRESULT close(void);                       /* Close an open file object */
    FRESULT truncate(void);                    /* Truncate file */
    FRESULT sync(void);                        /* Flush cached data of a writing file */

#if _USE_STRFUNC
    int     fputc (int);                       /* Put a character to the file */
    int     fputs (const char*);               /* Put a string to the file */
    int     fprintf (const char*, ...);        /* Put a formatted string to the file */
    char*   fgets (char*, int);                /* Get a string from the file */
    int     feof();
    int     ferror();
#endif
};

#ifndef EOF
#define EOF -1
#endif


#endif /* FATFILE_H */
