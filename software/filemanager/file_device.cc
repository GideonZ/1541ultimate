#include "file_device.h"
#include "file_partition.h"
#include <stdio.h>

FileDevice :: FileDevice(BlockDevice *b, const char *n, const char *dn) : CachedTreeNode(NULL, n)
{
    initialized = false;
    display_name = dn;
    blk = b;
    disk = NULL; //new Disk(b, 512);
    info.fs = NULL;
    info.cluster = 0; // indicate root dir
    info.attrib = AM_DIR; // ;-)
    info.name_format = NAME_FORMAT_DIRECT;
    isFloppy = false;
    //printf("FileDevice Created. This = %p, Disk = %p, blk = %p, name = %s, disp = %s, info = %p\n", this, disk, b, n, dn, get_file_info());
}

FileDevice :: ~FileDevice()
{
    //printf("Destructing FileDevice %p\n", this);
    detach_disk();
	if(info.fs) {
		delete info.fs;
    }
}

void FileDevice :: attach_disk(int block_size)
{
    //printf("&&& ATTACH %p &&&\n", disk);
    //printf("&&& %s &&&\n", get_name());
    if(disk) {
        printf("ERROR: DISK ALREADY EXISTS ON FILE DEVICE %s!\n", get_name());
        delete disk;
    }
    disk = new Disk(blk, block_size);
    initialized = false;
}

void FileDevice :: detach_disk(void)
{
    if(disk) {
        cleanup_children();
        delete disk;
    }
    disk = NULL;
    initialized = false;
}
    
bool FileDevice :: is_ready(void)
{
    t_device_state state = blk->get_state();
    return (state == e_device_ready);
}

int FileDevice :: probe(void)
{
	if (initialized)
		return children.get_elements();

	t_device_state state = blk->get_state();
    if(state != e_device_ready) {
        printf("Device is not ready to fetch children.\n");
        return -2;
    }
    if(!disk) {
        printf("No disk class attached!!\n");
        return -3;
    }

    // this function converts the partitions below the disk into browsable items
    int p_count = disk->Init(isFloppy);
    
    if(p_count < 0) {
        printf("Error initializing disk..%d\n", p_count);
        return -1;
    }

    initialized = true;

    Partition *p = disk->partition_list;
    if(p_count == 1) { // do not create partition in browser; that's not necessary!
        //printf("There is only one partition!! we can do this smarter!\n");
		info.fs = p->attach_filesystem();
		// printf("FileSystem = %p\n", info.fs);
		info.cluster = 0; // indicate root dir
		info.attrib = AM_DIR; // ;-)  (not read only of course, removed!!)
		if(!info.fs) {
			initialized = false;
			delete p;
			disk->partition_list = NULL;
			return -1;
		}
	    return 1;
    }

    info.fs = 0;

    int i = 0;
    while(p) {
        char pname[] = "Partx";
        pname[4] = '0'+i;

        children.append(new FilePartition(this, p, pname));
        p = p->next_partition;
        ++i;
    }
    return i;
}

void FileDevice :: get_display_string(char *buffer, int width)
{
	const char *c_state_string[] = { "Unknown", "No media", "Not ready", "Ready", "Error!", "\e2FAILED" };

	t_device_state state = blk->get_state();
/*
    uint32_t length;
    prt->ioctl(GET_SECTOR_COUNT, &length);
    size_to_string_sectors(length, sizebuf);
*/
    sprintf(buffer, "%8s%#s \e\x0d%s", get_name(), width-18, display_name, c_state_string[(int)state]);
}
