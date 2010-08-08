#include "file_device.h"
#include "file_partition.h"
#include "file_system.h"
#include "file_direntry.h"
#include "small_printf.h"
#include "fat_fs.h"
#include "size_str.h"

FilePartition :: FilePartition(PathObject *par, Partition *p, char *n) : FileDirEntry(par, NULL), name(n)
{
    prt = p; // link to partition object.
    info = new FileInfo(32);
}

FilePartition :: ~FilePartition()
{
	cleanup_children();
	if(info) {
		if(info->fs)
	        delete info->fs;
//		delete info; /* INFO will be deleted when we destruct the base class */
	}
}

char *FilePartition :: get_display_string(void)
{
    static char buffer[44];
    static char sizebuf[8];
    DWORD length;
    prt->ioctl(GET_SECTOR_COUNT, &length);
    size_to_string_sectors(length, sizebuf);
    small_sprintf(buffer, "\037%20s \027Size: %s", get_name(), sizebuf);
    return buffer;
}


void FilePartition :: init()
{
    // this function attaches the filesystem to the partition
    //if(info->fs)
    //    return; // already initialized. Filesystems do not suddenly change on any partition

    // for quick fix: Assume FATFS:
    BYTE res = FATFS :: check_fs(prt);
    printf("Checked FATFS: Result = %d\n", res);
    if(!res) {
        info->fs = new FATFS(prt);
        info->fs->init();
    } else {
        info->fs = NULL;
    }
    info->cluster = 0; // indicate root dir
    info->attrib = AM_DIR; // ;-)
}
    
int FilePartition :: fetch_children(void)
{
    init();
    if(!info->fs)
        return -1;

    int count = FileDirEntry :: fetch_children();  // we are just a normal directory, so..
    sort_children();
    return count;
}
