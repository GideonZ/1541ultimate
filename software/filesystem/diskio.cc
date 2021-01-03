#include "diskio.h"
#include "chanfat_manager.h"

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

extern "C"
DSTATUS disk_status (
        uint8_t pdrv		/* Physical drive nmuber to identify the drive */
)
{
    Partition *p = ChanFATManager::getPartition(pdrv);
    if (p) {
        return p->status();
    }
    return RES_NOTRDY;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

extern "C"
DSTATUS disk_initialize (
        uint8_t pdrv				/* Physical drive nmuber to identify the drive */
)
{
    Partition *p = ChanFATManager::getPartition(pdrv);
    if (p) {
        return p->status();
    }
    return RES_NOTRDY;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

extern "C"
DRESULT disk_read (
        uint8_t pdrv,	/* Physical drive nmuber to identify the drive */
        uint8_t *buff,		/* Data buffer to store read data */
        uint32_t sector,	/* Sector address in LBA */
        uint32_t count		/* Number of sectors to read */
)
{
    Partition *p = ChanFATManager::getPartition(pdrv);
    if (p) {
        return p->read(buff, sector, count);
    }
    return RES_NOTRDY;
}


/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if _USE_WRITE
extern "C"
DRESULT disk_write (
        uint8_t pdrv,		/* Physical drive nmuber to identify the drive */
        const uint8_t *buff,	/* Data to be written */
        uint32_t sector,		/* Sector address in LBA */
        uint32_t count			/* Number of sectors to write */
)
{
    Partition *p = ChanFATManager::getPartition(pdrv);
    if (p) {
        return p->write(buff, sector, count);
    }
    return RES_NOTRDY;
}
#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

#if _USE_IOCTL
extern "C"
DRESULT disk_ioctl (
        uint8_t pdrv,	/* Physical drive nmuber (0..) */
        uint8_t cmd,		/* Control code */
        void *buff		/* Buffer to send/receive control data */
)
{
    Partition *p = ChanFATManager::getPartition(pdrv);
    if (p) {
        return p->ioctl(cmd, buff);
    }
    return RES_NOTRDY;
}
#endif
