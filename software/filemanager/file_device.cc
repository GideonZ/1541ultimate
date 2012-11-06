#include "file_device.h"
#include "file_partition.h"
extern "C" {
    #include "small_printf.h"
}

FileDevice :: FileDevice(PathObject *p, BlockDevice *b, char *n) : FileDirEntry(p, n)
{
    blk = b;
    disk = NULL; //new Disk(b, 512);
}

FileDevice :: ~FileDevice()
{
    detach_disk();
    if(info) {
        if(info->fs)
            delete info->fs;
        delete info;
    }
	// now the children will be cleaned again the base class destructor.. (so be it)
    info = NULL;
//    printf("Done destructing %s\n", get_name());
}

void FileDevice :: attach_disk(int block_size)
{
    if(disk) {
        printf("ERROR: DISK ALREADY EXISTS ON FILE DEVICE %s!\n", get_name());
        delete disk;
    }
    disk = new Disk(blk, block_size);
}

void FileDevice :: detach_disk(void)
{
    if(disk) {
        cleanup_children();
        delete disk;
    }
    disk = NULL;
}
    
int FileDevice :: fetch_children(void)
{
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
    int p_count = disk->Init();
    
    if(p_count < 0) {
        printf("Error initializing disk..\n");
        return -1;
    }
    cleanup_children();

    Partition *p = disk->partition_list;
    if(p_count == 1) { // do not create partition in browser; that's not necessary!
        printf("There is only one partition!! we can do this smarter!\n");
        if(!info)
            info = new FileInfo(get_name());
        if(info) {
            info->fs = p->attach_filesystem();
            info->cluster = 0; // indicate root dir
            info->attrib = AM_DIR; // ;-)  (not read only of course, removed!!)
            if(!info->fs)
                return -1;
            int count = FileDirEntry :: fetch_children();  // we are in this case just a normal directory, so..
            sort_children();
            return count;
        } else {
            return -1;
        }
    }

    int i = 0;
    while(p) {
        char pname[] = "Partx";
        pname[4] = '0'+i;

		bool found = false;
		for(int x=0;x<children.get_elements();x++) {
			if(pattern_match(pname, children[x]->get_name(), false))
				found = true;
        }
        if(!found)
            children.append(new FilePartition(this, p, pname));

        p = p->next_partition;
        ++i;
    }
    return p_count;
}

char *FileDevice :: get_display_string(void)
{
    static char buffer[44];
//    static char sizebuf[8];

	const char *c_state_string[] = { "Unknown", "No media", "Not ready", "Ready", "Error!" };

	t_device_state state = blk->get_state();
/*
    DWORD length;
    prt->ioctl(GET_SECTOR_COUNT, &length);
    size_to_string_sectors(length, sizebuf);
*/
    small_sprintf(buffer, "%29s \025%s", get_name(), c_state_string[(int)state]);
    return buffer;
}
