
#include "file_system.h"
#include "pattern.h"

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

PathStatus_t FileSystem :: walk_path(PathInfo& pathInfo)
{
	//printf("WalkPath: '%s'\n", path);
	FRESULT fres;

	// example1: /sd/somedir/somefile.d64/hello.prg
	// first phase, the /sd is taken away from the path by the root file system.
	// second phase:
	// should terminate with e_TerminatedOnFile, where the remaining path = "/hello.prg",

	pathInfo.enterFileSystem(this);

	FileInfo info(32);
	Directory *dir;
	FileInfo *ninf;
	mstring workdir;

	while(pathInfo.hasMore()) {
		fres = dir_open(pathInfo.getPathFromLastFS(workdir), &dir, pathInfo.getLastInfo());
		if (fres == FR_OK) {
			while(1) {
				fres = dir_read(dir, &info);
				if (fres == FR_OK) {
					// printf("%9d: %-32s (%d)\n", info.size, info.lfname, info.cluster);
					if (info.attrib & AM_VOL)
						continue;
					if (pattern_match(pathInfo.workPath.getElement(pathInfo.index), info.lfname)) {
						dir_close(dir);
						pathInfo.replace(info.lfname);
						pathInfo.index++;
						ninf = pathInfo.getNewInfoPointer();
						ninf->copyfrom(&info);
						if (info.attrib & AM_DIR) {
							break;
						} else {
							if (pathInfo.hasMore())
								return e_TerminatedOnFile;
							else {
								return e_EntryFound;
							}
						}
					}
				} else { // not found
					pathInfo.index ++; // we will still return something, even if it doesn't exist (for file create)
					if (pathInfo.hasMore())
						return e_DirNotFound;
					else
						return e_EntryNotFound;
				}
			}
		}
	}
	return e_EntryFound;
}

bool FileSystem :: init(void)
{
    printf("Maybe you should not try to initialize a base class file system?\n");
	return false;
}
    
FRESULT FileSystem :: dir_open(const char *path, Directory **, FileInfo *inf)
{
    return FR_NO_FILESYSTEM;
}

void FileSystem :: dir_close(Directory *d)
{
	if (d)
		delete d;
}

FRESULT FileSystem :: dir_read(Directory *d, FileInfo *f)
{
    return FR_NO_FILESYSTEM;
}

FRESULT FileSystem :: dir_create(const char *path)
{
    return FR_NO_FILESYSTEM;
}
    
FRESULT FileSystem :: file_open(const char *path, Directory *, const char *filename, uint8_t flags, File **)
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
