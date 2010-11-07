#include "file_system.h"
#include "file.h"    
    
//FRESULT File :: open(FileInfo *, BYTE flags)
//{
//    return FR_DENIED;
//}

void    File :: close(void)
{
	if(!node) return; // TODO: how about deleting the handle?! Memory leak!!
    fs->file_close(this);
}

FRESULT File :: sync(void)
{
	if(!node) return FR_INVALID_OBJECT;
	return fs->file_sync(this);
}

FRESULT File :: read(void *buffer, DWORD len, UINT *transferred)
{
	if(!node) return FR_INVALID_OBJECT;
	return fs->file_read(this, buffer, len, transferred);
}

FRESULT File :: write(void *buffer, DWORD len, UINT *transferred)
{
	if(!node) return FR_INVALID_OBJECT;
    return fs->file_write(this, buffer, len, transferred);
}

FRESULT File :: seek(DWORD pos)
{
	if(!node) return FR_INVALID_OBJECT;
    return fs->file_seek(this, pos);
}
