
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

PathStatus_t FileSystem :: walk_path(Path *path, const char *fn, PathInfo& pathInfo)
{
	printf("WalkPath: '%s':'%s'\n", path->get_path(), fn);
	FRESULT fres;

	// example1: /sd/somedir/somefile.d64/hello.prg
	// first phase, the /sd is taken away from the path by the root file system.
	// second phase:
	// should terminate with e_TerminatedOnFile, where the remaining path = "/hello.prg",

	pathInfo.remainingPath.cd(path->get_path());
	for(int i=0;i<path->getDepth();i++) {
		fres = file_stat(&pathInfo.pathFromRootOfFileSystem, path->getElement(i), &pathInfo.fileInfo);
		if (fres != FR_OK) {
			return e_DirNotFound;
		}
		pathInfo.pathFromRootOfFileSystem.cd(path->getElement(i));
		pathInfo.remainingPath.removeFirst();
		if (!(pathInfo.fileInfo.attrib & AM_DIR)) {
			return e_TerminatedOnFile;
		}
	}
	fres = file_stat(&pathInfo.pathFromRootOfFileSystem, fn, &pathInfo.fileInfo);
	if (fres == FR_OK) {
		return e_EntryFound;
	}
	return e_EntryNotFound;
}

FRESULT FileSystem :: file_stat(Path *path, const char *fn, FileInfo *inf)
{
	return FR_NO_FILESYSTEM;
}

bool FileSystem :: init(void)
{
    printf("Maybe you should not try to initialize a base class file system?\n");
	return false;
}
    
FRESULT FileSystem :: dir_open(const char *path, Directory **)
{
    return FR_NO_FILESYSTEM;
}

void FileSystem :: dir_close(Directory *d)
{
}

FRESULT FileSystem :: dir_read(Directory *d, FileInfo *f)
{
    return FR_NO_FILESYSTEM;
}

FRESULT FileSystem :: dir_create(const char *path)
{
    return FR_NO_FILESYSTEM;
}
    
FRESULT FileSystem :: file_open(const char *path, uint8_t flags, File **)
{
    return FR_NO_FILESYSTEM;
}

void    FileSystem :: file_close(File *f)
{
}

FRESULT FileSystem :: file_read(File *f, void *buffer, uint32_t len, uint32_t *transferred)
{
    *transferred = 0;
    return FR_NO_FILESYSTEM;
}

FRESULT FileSystem :: file_write(File *f, void *buffer, uint32_t len, uint32_t *transferred)
{
    *transferred = 0;
    return FR_NO_FILESYSTEM;
}

FRESULT FileSystem :: file_seek(File *f, uint32_t pos)
{
    return FR_NO_FILESYSTEM;
}

FRESULT FileSystem :: file_sync(File *f)
{
    return FR_NO_FILESYSTEM;
}

FRESULT FileSystem :: file_rename(const char *old_name, const char *new_name)
{
    return FR_NO_FILESYSTEM;
}

FRESULT FileSystem :: file_delete(const char *name)
{
    return FR_NO_FILESYSTEM;
}
