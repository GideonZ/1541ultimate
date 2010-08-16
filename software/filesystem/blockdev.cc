
#include "blockdev.h"

BlockDevice::BlockDevice()
{
	dev_state = e_device_unknown;
}
    
BlockDevice::~BlockDevice()
{
	dev_state = e_device_unknown;
}

DSTATUS BlockDevice::init(void)
{
    // We don't need to do anything here, just return status
    return status();
}
        
DSTATUS BlockDevice::status(void)
{
    return STA_NODISK;
}
    
DRESULT BlockDevice::read(BYTE *buffer, DWORD sector, BYTE count)
{
    return RES_NOTRDY;
}

#if	_READONLY == 0
DRESULT BlockDevice::write(const BYTE *buffer, DWORD sector, BYTE count)
{
    return RES_NOTRDY;
}
#endif

DRESULT BlockDevice::ioctl(BYTE command, void *data)
{
    return RES_NOTRDY;
}
