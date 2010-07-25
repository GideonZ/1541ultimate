#ifndef SD_H
#define SD_H
#include "blockdev.h" // base class definition
#include "sdio.h"

#define CMDGETSTATUS 13
#define CMDSETBLKLEN 16
#define	CMDREAD      17
#define	CMDWRITE     24
#define	CMDREADCSD    9
#define	CMDREADCID   10
#define CMDCRCONOFF  59
#define CMDAPPCMD    55
#define CMDREADOCR   58

#define SD_SECTOR_SIZE 512

class SdCard : public BlockDevice
{
    int     sd_type;
    bool    sdhc;
    bool    initialized;
    
    /* Private functions */

    BYTE    Resp8b(void);
    WORD    Resp16b(void);
    void    Resp8bError(BYTE value);
//    int     get_state(void);
    DRESULT verify(const BYTE* buf, DWORD address );
    DRESULT get_drive_size(DWORD* drive_size);

public: /* block device api */
    SdCard();
    ~SdCard();

    DSTATUS init(void);
    DSTATUS status(void);
    DRESULT read(BYTE *, DWORD, BYTE);
    DRESULT write(const BYTE *, DWORD, BYTE);
    DRESULT ioctl(BYTE, void *);
};

#endif
