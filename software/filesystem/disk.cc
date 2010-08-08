
#include "disk.h"
#include "small_printf.h"
    
Disk::Disk(BlockDevice *b, int sec = 512)
{
    dev = b;
    sector_size = sec;
    buf = new BYTE[sec];
    partition_list = NULL;
}

Disk::~Disk()
{
    Partition *prt, *next;
    if(buf)
        delete buf;

    prt = partition_list;
    while(prt) {
        next = prt->next_partition;
        delete prt;
        prt = next;
    }
}

int Disk::Init(void)
{
    bool fat = false;

    // if present, delete existing partition list
    Partition *prt;
    Partition **prt_list;
    DWORD lba, start, size, next;
    BYTE *tbl;
    
#ifdef BOOTLOADER
    if(dev->init())
        return -1; // can't initialize block device
#else
	if(dev->get_state() != e_device_ready) {
		printf("Disk::Init, dev = %p, dev state = %d\n", dev, dev->get_state());
		return -1;
	}
#endif
	    
    // Read Master Boot Record and check for validity
    if(dev->read(buf, 0L, 1) != RES_OK)
        return -2; // disk unreadable

    if(LD_WORD(buf + BS_Signature) != 0xAA55)
        return -3; // disk unformatted 

    /* Handle special case in which there is no partition table present */
    /* This can only be the case when it is a FAT file system */
    dev->ioctl(GET_SECTOR_COUNT, &size);

    printf("Get sector count: %d\n", size);    

	if ((LD_DWORD(&buf[BS_FilSysType]) & 0xFFFFFF) == 0x544146)	{ /* Check "FAT" string */
        partition_list = new Partition(dev, 0L, size, 0x06);
        return 1; // one default partition, starting on sector 0
    }
    
	if ((LD_DWORD(&buf[BS_FilSysType32]) & 0xFFFFFF) == 0x544146) {
        partition_list = new Partition(dev, 0L, size, 0x0C);
        return 1; // one default partition, starting on sector 0
    }
    
    //printf("Going to read partition table.\n");
    
    prt_list = &partition_list; // the first entry should be stored here in the class
    lba = 0L;
    int p_count = 0;        

    // walk through partition tables
    do {
        tbl = &buf[MBR_PTable];
        if(tbl[4]) {
            start = LD_DWORD(&tbl[8]);
            size  = LD_DWORD(&tbl[12]);
            prt = new Partition(dev, lba + start, size, tbl[4]);
            *prt_list = prt;
            prt_list = &prt->next_partition;
            p_count ++;
        }
        next = LD_DWORD(&tbl[24]);
        lba += next;
        if(next) { // read next EBR
            if(dev->read(buf, sector_size, 1) != RES_OK)
                return p_count;
        }
    } while(next);

    return p_count;        
}

