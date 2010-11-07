#include "file_device.h"
#include "file_partition.h"
#include "small_printf.h"

FileDevice :: FileDevice(PathObject *p, BlockDevice *b, char *n) : PathObject(p, n)
{
    blk = b;
    disk = new Disk(b, 512);
}

FileDevice :: ~FileDevice()
{
	cleanup_children(); // THIS IS ABSOLUTELY NECESSARY..
	// because our children are dependent and refer to this disk:
	if(disk)
        delete disk;
	// now the children will be cleaned again the base class destructor.. (so be it)
}

int FileDevice :: fetch_children(void)
{
    // this function converts the partitions below the disk into browsable items
    if(disk->Init() < 0) {
        printf("Error initializing disk..\n");
        return -1;
    }
    //printf("Disk Initialized.\n");
    cleanup_children();
    Partition *p = disk->partition_list;
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

        ++i;
        p = p->next_partition;
    }
    return i;
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
    small_sprintf(buffer, "\037%29s \025%s", get_name(), c_state_string[(int)state]);
    return buffer;
}
