#include <stdio.h>
#include "file_device.h"
#include "file_partition.h"

#include "../components/size_str.h"
#include "file_system.h"


FilePartition :: FilePartition(CachedTreeNode *par, Partition *p, char *n) : CachedTreeNode(par, n)
{
    prt = p; // link to partition object.
    info.cluster = 0; // indicate root dir
    info.attrib = AM_DIR; // ;-)
    info.special_display = 1;
}

FilePartition :: ~FilePartition()
{
	cleanup_children();
	if(info.fs) {
		delete info.fs;
	}
}

const char *FilePartition :: get_type_string(uint8_t typ)
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

void FilePartition :: get_display_string(char *buffer, int width)
{
    static char sizebuf[8];
    uint32_t length;
    prt->ioctl(GET_SECTOR_COUNT, &length);
    size_to_string_sectors(length, sizebuf);
    sprintf(buffer, "%#s \e7%s \eJ%s", width-16, get_name(), sizebuf, get_type_string(prt->get_type()));
}

void FilePartition :: init()
{
    info.fs = prt->attach_filesystem();
}
    
/*
int FilePartition :: fetch_children(void)
{
    init();
    if(!info.fs)
        return -1;

    int count = FileDirEntry :: fetch_children();  // we are just a normal directory, so..
    sort_children();
    return count;
}
*/
