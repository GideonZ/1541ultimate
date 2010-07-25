/*-----------------------------------------------------------------------
/  Partition Interface
/-----------------------------------------------------------------------*/
#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#define _READONLY	0	/* 1: Read-only mode */
#define _USE_IOCTL	1

#include "integer.h"

/* Status of Disk Functions */
typedef BYTE	DSTATUS;

/* Results of Disk Functions */
typedef enum {
	RES_OK = 0,		/* 0: Successful */
	RES_ERROR,		/* 1: R/W Error */
	RES_WRPRT,		/* 2: Write Protected */
	RES_NOTRDY,		/* 3: Not Ready */
	RES_PARERR		/* 4: Invalid Parameter */
} DRESULT;

/* Disk Status Bits (DSTATUS) */

#define STA_NOINIT		0x01	/* Drive not initialized */
#define STA_NODISK		0x02	/* No medium in the drive */
#define STA_PROTECT		0x04	/* Write protected */


/* Command code for ioctl() */

/* Generic command */
#define CTRL_SYNC			1	/* Mandatory for write functions */
#define GET_SECTOR_COUNT	2	/* Mandatory for only f_mkfs() */
#define GET_SECTOR_SIZE		3
#define GET_BLOCK_SIZE		4	/* Mandatory for only f_mkfs() */
#define CTRL_POWER			5
#define CTRL_LOCK			6
#define CTRL_EJECT			7

/* Device state */
typedef enum {
	e_device_unknown = 0,
	e_device_no_media,
	e_device_not_ready,
	e_device_ready,
	e_device_error
} t_device_state;

class BlockDevice
{
//    FILE *f;
//    int file_size;
//    int sector_size;
	t_device_state dev_state;
public:
    BlockDevice();
    virtual ~BlockDevice();
    t_device_state get_state(void)   { return dev_state; }
    void set_state(t_device_state s) { dev_state = s; }

    virtual DSTATUS init(void);
    virtual DSTATUS status(void);
    virtual DRESULT read(BYTE *, DWORD, BYTE);
#if	_READONLY == 0
    virtual DRESULT write(const BYTE *, DWORD, BYTE);
#endif
    virtual DRESULT ioctl(BYTE, void *);
    
};

#endif
