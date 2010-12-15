
#include "disk.h"
    
extern "C" {
    #include "small_printf.h"
	#include "dump_hex.h"
}

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

    if(LD_WORD(buf + BS_Signature) != 0xAA55) {
        if(dev->ioctl(GET_SECTOR_COUNT, &size) == RES_OK) {
            partition_list = new Partition(dev, 0L, size, 0x00);
            return 1; // one default partition, starting on sector 0
        }
        return -3; // disk unformatted or can't even create a default partition
    }

    // Handle special case in which there is no partition table present 
    // This can only be the case when it is a FAT file system 

	if ((LD_DWORD(&buf[BS_FilSysType]) & 0xFFFFFF) == 0x544146)	{ // Check "FAT" string
        return -4; // can't read device size
    }
    
	if ((LD_DWORD(&buf[BS_FilSysType32]) & 0xFFFFFF) == 0x544146) {
        if(dev->ioctl(GET_SECTOR_COUNT, &size) == RES_OK) {
            partition_list = new Partition(dev, 0L, size, 0x0C);
            return 1; // one default partition, starting on sector 0
        }
        return -4; // can't read device size
    }
    
    //printf("Going to read partition table.\n");
    
    prt_list = &partition_list; // the first entry should be stored here in the class
    lba = 0L;
    p_count = 0;        

    // walk through partition tables
    tbl = &buf[MBR_PTable];
//    dump_hex(tbl, 66);

    for(int p=0;p<4;p++) {
        if(tbl[4]) {
            start = LD_DWORD(&tbl[8]);
            size  = LD_DWORD(&tbl[12]);
            if(tbl[4] == 0x0F) {
                printf("Result of EBR read: %d.\n", read_ebr(&prt_list, start));
            } else {
                prt = new Partition(dev, lba + start, size, tbl[4]);
                *prt_list = prt;
                prt_list = &prt->next_partition;
                p_count ++;
            }
        }
        tbl += 16;
    }            
    return p_count;        
}

int Disk :: read_ebr(Partition ***prt_list, DWORD lba)
{
    BYTE *local_buf = new BYTE[sector_size];
    Partition *prt;
    DWORD start, size;
    BYTE *tbl;

    if(dev->read(local_buf, lba, 1) != RES_OK) {
        delete local_buf;
        return -2; // partition unreadable
    }
    
    if(LD_WORD(local_buf + BS_Signature) != 0xAA55) {
        delete local_buf;
        return -3; // partition unformatted 
    }
            
    tbl = &local_buf[MBR_PTable];
//    dump_hex(tbl, 66);

    for(int p=0;p<4;p++) {
        if(tbl[4]) {
            start = LD_DWORD(&tbl[8]);
            size  = LD_DWORD(&tbl[12]);
            if(tbl[4] == 0x0F) {
                read_ebr(prt_list, start);
            } else {
                prt = new Partition(dev, lba + start, size, tbl[4]);
                **prt_list = prt;
                *prt_list = &prt->next_partition;
                p_count ++;
            }
        }
        tbl += 16;
    }            
    delete local_buf;
    return 0;
}
