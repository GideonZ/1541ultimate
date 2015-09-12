#include "diskio.h"
#include "partition.h"

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

extern "C"
DSTATUS disk_status (
		DRVREF pdrv		/* Physical drive nmuber to identify the drive */
)
{
	return ((Partition *)pdrv)->status();
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

extern "C"
DSTATUS disk_initialize (
		DRVREF pdrv				/* Physical drive nmuber to identify the drive */
)
{
	return ((Partition *)pdrv)->status();
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

extern "C"
DRESULT disk_read (
	DRVREF pdrv,	/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	DWORD sector,	/* Sector address in LBA */
	UINT count		/* Number of sectors to read */
)
{
	return ((Partition *)pdrv)->read(buff, sector, count);
}


/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if _USE_WRITE
extern "C"
DRESULT disk_write (
	DRVREF pdrv,		/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	DWORD sector,		/* Sector address in LBA */
	UINT count			/* Number of sectors to write */
)
{
	return ((Partition *)pdrv)->write(buff, sector, count);
}
#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

#if _USE_IOCTL
extern "C"
DRESULT disk_ioctl (
	DRVREF pdrv,	/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	return ((Partition *)pdrv)->ioctl(cmd, buff);
}
#endif
