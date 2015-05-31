#ifndef FATFILE_H
#define FATFILE_H

#include "fat_fs.h"
#include "fat_dir.h"
#include "file_info.h"

/* File object structure */
class FATFIL { /* this is a FAT file.. and should be derived from generic FILE class. */
private:
    FATFS*  fs;         /* Pointer to the owner file system object */
    uint8_t    valid;      /* Set to a non-zero value when file is successfully opened */
    uint8_t    flag;       /* File status flags */
    uint8_t    csect;      /* Sector address in the cluster */
    uint32_t   fptr;       /* File R/W pointer */
    uint32_t   fsize;      /* File size */
    uint32_t   org_clust;  /* File start cluster */
    uint32_t   curr_clust; /* Current cluster */
    uint32_t   dsect;      /* Current data sector */
#if !_FS_READONLY
    FATDIR *dir_obj;    /* Information about the directory entry that points to this file */
#endif
#if !_FS_TINY
    uint8_t    buf[_MAX_SS];/* File R/W buffer */
#endif
public:
    FATFIL(FATFS *);                /* constructor */
    FATFIL(FATFS *, XCHAR *, uint8_t); /* constructor, calls open, too */
    ~FATFIL();                      /* destructor */

    void print_info(void);			// prints all variables for debug
    FRESULT validate(void);                    /* Check if everything is still ok */
    FRESULT open(XCHAR*, uint32_t s, uint8_t, uint16_t *dir_index);  /* Open or create a file */
    FRESULT open(FileInfo *fi, uint8_t);          /* Open or create a file, using a known dir entry */
    FRESULT read(void*, uint32_t, uint32_t*);          /* Read data from a file */
    FRESULT write(const void*, uint32_t, uint32_t*);   /* Write data to a file */
    FRESULT lseek(uint32_t);                      /* Move file pointer of a file object */
    FRESULT close(void);                       /* Close an open file object */
    FRESULT truncate(void);                    /* Truncate file */
    FRESULT sync(void);                        /* Flush cached data of a writing file */

    void update_info(FileInfo *info) {
    	if(!info)
    		return;
    	info->size = fsize;
    }

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
