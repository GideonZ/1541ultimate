extern "C" {
    #include "small_printf.h"
}
#include "file_device.h"
#include "file_partition.h"
#include "file_system.h"
#include "file_direntry.h"
#include "size_str.h"


FilePartition :: FilePartition(PathObject *par, Partition *p, char *n) : FileDirEntry(par, n) //, name(n)
{
    prt = p; // link to partition object.
    info = new FileInfo(16);
    strcpy(info->lfname, n); // redundant, oh well!
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

char *FilePartition :: get_type_string(BYTE typ)
{
    switch(typ) {
        case 0x00: return "None";
        case 0x01: return "FAT12";
        case 0x06: return "FAT16";
        case 0x07: return "NTFS";
        case 0x0B: return "FAT32";
        case 0x0C: return "FAT32";
        case 0x82: return "Swap";
        case 0x83: return "Linux";
        default: return "??";
    }
    return "";
}

char *FilePartition :: get_display_string(void)
{
    static char buffer[44];
    static char sizebuf[8];
    DWORD length;
    prt->ioctl(GET_SECTOR_COUNT, &length);
    size_to_string_sectors(length, sizebuf);
    small_sprintf(buffer, "%24s \027%s \032%s", get_name(), sizebuf, get_type_string(prt->get_type()));
    return buffer;
}

void FilePartition :: init()
{
    info->fs = prt->attach_filesystem();
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
