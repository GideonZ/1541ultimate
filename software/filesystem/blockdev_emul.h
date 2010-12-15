/*-----------------------------------------------------------------------
/  Partition Interface
/-----------------------------------------------------------------------*/
#ifndef BLOCKDEV_EMUL_H
#define BLOCKDEV_EMUL_H
#include "blockdev.h"

class BlockDevice_Emulated : public BlockDevice
{
    FILE *f;
    long file_size;
    int sector_size;
	t_device_state dev_state;
public:
    BlockDevice_Emulated(char *name, int sec_size);
    virtual ~BlockDevice_Emulated();
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
