#include "file_system.h"
#include "file.h"    
    
//FRESULT File :: open(FileInfo *, BYTE flags)
//{
//    return FR_DENIED;
//}

void    File :: close(void)
{
	if(!filesystem) return;
    filesystem->file_close(this);
}

FRESULT File :: sync(void)
{
	if(!filesystem) return FR_INVALID_OBJECT;
	return filesystem->file_sync(this);
}

FRESULT File :: read(void *buffer, uint32_t len, uint32_t *transferred)
{
	//printf("File read: %p: %d to %p. Res=", this, len, buffer);
	if(!filesystem) return FR_INVALID_OBJECT;
	FRESULT res = filesystem->file_read(this, buffer, len, transferred);
	//printf("%d (%d)\n", res, *transferred);
	return res;
}

FRESULT File :: write(const void *buffer, uint32_t len, uint32_t *transferred)
{
	if(!filesystem) return FR_INVALID_OBJECT;
    return filesystem->file_write(this, buffer, len, transferred);
}

FRESULT File :: seek(uint32_t pos)
{
	if(!filesystem) return FR_INVALID_OBJECT;
    return filesystem->file_seek(this, pos);
}

