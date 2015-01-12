
#include "file_system.h"

bool FileInfo :: is_writable(void)
{
    if(!fs) { // there is no file system, so it's certainly not writable
        return false;
    }
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
			return "DISK ERROR";
		case FR_INT_ERR:
			return "INTERNAL ERROR";
		case FR_NOT_READY:
			return "DEVICE NOT READY";
		case FR_NO_FILE:
			return "FILE DOESN'T EXIST";
		case FR_NO_PATH:
			return "PATH DOESN'T EXIST";
		case FR_INVALID_NAME:
			return "INVALID NAME";
		case FR_DENIED:
			return "ACCESS DENIED";
		case FR_EXIST:
			return "FILE EXISTS";
		case FR_INVALID_OBJECT:
			return "INVALID OBJECT";
		case FR_WRITE_PROTECTED:
			return "WRITE PROTECTED";
		case FR_INVALID_DRIVE:
			return "INVALID DRIVE"; // obsolete
		case FR_NOT_ENABLED:
			return "NOT ENABLED";
		case FR_NO_FILESYSTEM:
			return "NO FILESYSTEM";
		case FR_MKFS_ABORTED:
			return "MAKE FILESYSTEM ABORTED";
		case FR_TIMEOUT:
			return "I/O TIMEOUT";
		case FR_NO_MEMORY:
			return "OUT OF MEMORY";
		case FR_DISK_FULL:
			return "DISK IS FULL";
		case FR_DIR_NOT_EMPTY:
			return "DIRECTORY NOT EMPTY";
		default:
			return "UNKNOWN ERROR";
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
