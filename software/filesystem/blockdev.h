/*-----------------------------------------------------------------------
/  Partition Interface
/-----------------------------------------------------------------------*/
#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#define _READONLY	0	/* 1: Read-only mode */
#define _USE_IOCTL	1

#include <stdint.h>
#include "diskio.h"

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
	t_device_state dev_state;
public:
    BlockDevice();
    virtual ~BlockDevice();
    t_device_state get_state(void)   { return dev_state; }
    void set_state(t_device_state s) { dev_state = s; }

    virtual DSTATUS init(void);
    virtual DSTATUS status(void);
    virtual DRESULT read(uint8_t *, uint32_t, int);
#if	_READONLY == 0
    virtual DRESULT write(const uint8_t *, uint32_t, int);
#endif
    virtual DRESULT ioctl(uint8_t, void *);
    
};

#endif
