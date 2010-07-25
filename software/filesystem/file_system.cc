
#include "file_system.h"

FileSystem :: FileSystem(Partition *p)
{
    prt = p;
}
    
FileSystem :: ~FileSystem()
{
}


bool FileSystem :: check(Partition *p)
{
    return false; // base class never gives a match
}
    
void FileSystem :: init(void)
{
    return;
}
    
FRESULT FileSystem :: get_free (DWORD*)
{
    return FR_DENIED;
}
   
FRESULT FileSystem :: sync(void)
{
    return FR_DENIED;
}

Directory *FileSystem :: dir_open(FileInfo *)
{
    return NULL;
}

void FileSystem :: dir_close(Directory *d)
{
}

FRESULT FileSystem :: dir_read(Directory *d, FileInfo *f)
{
    return FR_DENIED;
}

FRESULT FileSystem :: dir_create(FileInfo *f)
{
    return FR_DENIED;
}
    
File   *FileSystem :: file_open(FileInfo *, BYTE flags)
{
    return NULL;
}

void    FileSystem :: file_close(File *f)
{
}

FRESULT FileSystem :: file_read(File *f, void *buffer, DWORD len, UINT *transferred)
{
    *transferred = 0;
    return FR_DENIED;
}

FRESULT FileSystem :: file_write(File *f, void *buffer, DWORD len, UINT *transferred)
{
    *transferred = 0;
    return FR_DENIED;
}

FRESULT FileSystem :: file_seek(File *f, DWORD pos)
{
    return FR_DENIED;
}

FRESULT FileSystem :: file_rename(FileInfo *, char *new_name)
{
    return FR_DENIED;
}
