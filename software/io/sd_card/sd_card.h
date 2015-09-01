#ifndef SD_H
#define SD_H
#include "blockdev.h" // base class definition
#include "sdio.h"
#include "FreeRTOS.h"
#include "semphr.h"

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
	SemaphoreHandle_t mutex;
    
    /* Private functions */

    uint8_t    Resp8b(void);
    uint16_t   Resp16b(void);
    void    Resp8bError(uint8_t value);
    DRESULT verify(const uint8_t* buf, uint32_t address );
    DRESULT get_drive_size(uint32_t* drive_size);

public: /* block device api */
    SdCard();
    ~SdCard();

    DSTATUS init(void);
    DSTATUS status(void);
    DRESULT read(uint8_t *, uint32_t, int);
    DRESULT write(const uint8_t *, uint32_t, int);
    DRESULT ioctl(uint8_t, void *);
};

#endif
