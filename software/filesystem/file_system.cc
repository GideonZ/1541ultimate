
#include "file_system.h"

bool FileInfo :: is_writable(void)
{
//    printf("Checking writability of %s.\n", lfname);
    if(!fs->is_writable()) {
        printf("%s: FileSystem is not writable..\n", lfname);
        return false;
    }
//    printf("Attrib: %b\n", attrib);
    return !(attrib & AM_RDO);
}
	
FileSystem :: FileSystem(Partition *p)
{
    prt = p;
}
    
FileSystem :: ~FileSystem()
{
}

const char *FileSystem :: get_error_string(FRESULT res)
{
	switch(res) {
		case FR_OK:
			return "OK!";
		case FR_DISK_ERR:
			return "Disk error";
		case FR_INT_ERR:
			return "Internal error";
		case FR_NOT_READY:
			return "Device not ready";
		case FR_NO_FILE:
			return "File doesn't exist";
		case FR_NO_PATH:
			return "Path doesn't exist";
		case FR_INVALID_NAME:
			return "Invalid name";
		case FR_DENIED:
			return "Access denied";
		case FR_EXIST:
			return "File exists";
		case FR_INVALID_OBJECT:
			return "Invalid Object";
		case FR_WRITE_PROTECTED:
			return "Write protected";
		case FR_INVALID_DRIVE:
			return "Invalid drive"; // obsolete
		case FR_NOT_ENABLED:
			return "Not enabled";
		case FR_NO_FILESYSTEM:
			return "No Filesystem";
		case FR_MKFS_ABORTED:
			return "Make FileSystem Aborted";
		case FR_TIMEOUT:
			return "I/O Timeout";
		case FR_NO_MEMORY:
			return "Out of memory";
		case FR_DISK_FULL:
			return "Disk is full";
		case FR_DIR_NOT_EMPTY:
			return "Directory not empty";
		default:
			return "Unknown error";
	}
	return "";
}


bool FileSystem :: check(Partition *p)
{
    return false; // base class never gives a match
}
    
bool FileSystem :: init(void)
{
    return false;
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

FRESULT FileSystem :: file_sync(File *f)
{
    return FR_DENIED;
}

FRESULT FileSystem :: file_rename(FileInfo *, char *new_name)
{
    return FR_DENIED;
}

FRESULT FileSystem :: file_delete(FileInfo *)
{
    return FR_DENIED;
}
