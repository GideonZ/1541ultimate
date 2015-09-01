/*
 * fs_errors_flags.h
 *
 *  Created on: Aug 29, 2015
 *      Author: Gideon
 */

#ifndef FILESYSTEM_FS_ERRORS_FLAGS_H_
#define FILESYSTEM_FS_ERRORS_FLAGS_H_


/* File function return code (FRESULT) */
typedef enum {
	FR_OK = 0,				/* (0) Succeeded */
	FR_DISK_ERR,			/* (1) A hard error occurred in the low level disk I/O layer */
	FR_INT_ERR,				/* (2) Assertion failed */
	FR_NOT_READY,			/* (3) The physical drive cannot work */
	FR_NO_FILE,				/* (4) Could not find the file */
	FR_NO_PATH,				/* (5) Could not find the path */
	FR_INVALID_NAME,		/* (6) The path name format is invalid */
	FR_DENIED,				/* (7) Access denied due to prohibited access or directory full */
	FR_EXIST,				/* (8) Access denied due to prohibited access */
	FR_INVALID_OBJECT,		/* (9) The file/directory object is invalid */
	FR_WRITE_PROTECTED,		/* (10) The physical drive is write protected */
	FR_INVALID_DRIVE,		/* (11) The logical drive number is invalid */
	FR_NOT_ENABLED,			/* (12) The volume has no work area */
	FR_NO_FILESYSTEM,		/* (13) There is no valid FAT volume */
	FR_MKFS_ABORTED,		/* (14) The f_mkfs() aborted due to any parameter error */
	FR_TIMEOUT,				/* (15) Could not get a grant to access the volume within defined period */
	FR_LOCKED,				/* (16) The operation is rejected according to the file sharing policy */
	FR_NO_MEMORY,	   		/* (17) LFN working buffer could not be allocated */
	FR_TOO_MANY_OPEN_FILES,	/* (18) Number of open files > _FS_SHARE */
	FR_INVALID_PARAMETER,	/* (19) Given parameter is invalid */
	FR_DISK_FULL,			/* (20) OLD FATFS: no more free clusters */
	FR_DIR_NOT_EMPTY		/* (21) Directory not empty */
} FRESULT;

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




#endif /* FILESYSTEM_FS_ERRORS_FLAGS_H_ */
