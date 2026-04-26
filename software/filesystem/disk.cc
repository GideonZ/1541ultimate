#include <string.h>

#include "disk.h"
    
extern "C" {
    #include "small_printf.h"
	#include "dump_hex.h"
}

Disk::Disk(BlockDevice *b, int sec = 512)
{
    partition_list = NULL;
    last_partition = NULL;
    dev = b;
    sector_size = sec;
    p_count = 0;
    buf = new uint8_t[sec];
}

Disk::~Disk()
{
    Partition *prt, *next;
    if(buf)
         delete[] buf;

    prt = partition_list;
    while(prt) {
        next = prt->next_partition;
        delete prt;
        prt = next;
    }
}

int Disk::Init(bool isFloppy)
{
    bool fat = false;

    // if present, delete existing partition list
    uint32_t lba, start, size, next;
    uint8_t *tbl;
    
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

    if(isFloppy) {
        if(dev->ioctl(GET_SECTOR_COUNT, &size) == RES_OK) {
            partition_list = new Partition(dev, 0L, size, 0x01);
            return 1; // one default partition, starting on sector 0
        }
        return -3;
    }

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
        if(dev->ioctl(GET_SECTOR_COUNT, &size) == RES_OK) {
            partition_list = new Partition(dev, 0L, size, 0x06);
            printf("Returning 1 partition\n");
            return 1; // one default partition, starting on sector 0
        }
        return -4; // can't read device size
    }
    
	if ((LD_DWORD(&buf[BS_FilSysType32]) & 0xFFFFFF) == 0x544146) {
        if(dev->ioctl(GET_SECTOR_COUNT, &size) == RES_OK) {
            partition_list = new Partition(dev, 0L, size, 0x0C);
            return 1; // one default partition, starting on sector 0
        }
        return -4; // can't read device size
    }
    
    printf("Going to read partition table. partition_list is %p\n", partition_list);
    
    lba = 0L;
    p_count = 0;        

    // walk through partition tables
    tbl = &buf[MBR_PTable];
//    dump_hex(tbl, 66);

    for(int p=0;p<4;p++) {
        uint8_t parttype = tbl[4];
        if (parttype) {
            start = LD_DWORD(&tbl[8]);
            size  = LD_DWORD(&tbl[12]);
            printf("MBR Start: %d Size: %d Type: %d\n", start, size, parttype);
            if (parttype == 0xEE) {
                printf("MBR Partition %d is of type Blocker (Protective MBR, disk likely uses GPT!), skipping\n", p);
            }
            else if ((parttype == 0x0F) || (parttype == 0x05)) {
                printf("Result of EBR read: %d.\n", read_ebr(start));
            } else {
                register_partition(lba + start, size, parttype);
            }
        }
        tbl += 16;
    } 

    // If we didn't find any MBR partitions we check for GPT
    if(!p_count && dev->read(buf, 1L, 1) != RES_OK) {
        return -2; // disk unreadable
    }
    if (!p_count && (memcmp(&buf[GPT_SIGNATURE], "EFI PART", 8) == 0 ||
            memcmp(&buf[GPT_REVISION], "\0\0\x01\0", 4) == 0 ||
            memcmp(&buf[GPT_PARTITION_ARRAY_LBA_HI], "\0\0\0\0", 4) == 0)) {  // Only supporting array on 32 bit LBA for now
        printf("GPT disk found, reading partition table\n");
        uint32_t sector = LD_WORD(&buf[GPT_PARTITION_ARRAY_LBA_LO]);
        uint32_t num_entries = LD_WORD(&buf[GPT_PARTITION_ARRAY_COUNT]);
        uint32_t entry_size = LD_WORD(&buf[GPT_PARTITION_ENTRY_SIZE]);
        uint32_t pos = sector_size;
        for (int part=0; part < num_entries; ++part) {
            if (pos >= sector_size) {
                if(dev->read(buf, sector, 1) != RES_OK) {
                    printf("Unable to read next block for GPT Partition Array (%d), ignoring disk to be safe!\n", sector);
                    return -2;
                }
                ++sector;
                pos = 0;
            }
            const uint8_t *entry = &buf[pos];
            pos += entry_size;
            if (memcmp(&entry[GPT_ENTRY_TYPE_GUID], GUID_MICROSOFT_BASIC_DATA, 16) == 0) {
                if(memcmp(&entry[GPT_ENTRY_FIRST_LBA_HI], "\0\0\0\0", 4) != 0 ||
                        memcmp(&entry[GPT_ENTRY_LAST_LBA_HI], "\0\0\0\0", 4) != 0) {
                    printf("GPT partition %d needs 64 bit LBA which is not yet supported, skipping!\n", part);
                    continue;
                }
                uint8_t attributes7 = entry[GPT_ENTRY_ATTRIBUTES + 7];  // MSB, bit 56-63
                if (attributes7 & (GPT_PARTITION_IGNORE_ATTRIBUTES)) {
                    printf("GPT partition %d has attributes we do not support, skipping attributes 0x", part);
                    for (int i=0; i < 16; ++i) {
                        printf("%02x", entry[GPT_ENTRY_ATTRIBUTES + i]);
                    }
                    printf("\n");
                    continue;
                }
                uint32_t first = LD_DWORD(&entry[GPT_ENTRY_FIRST_LBA_LO]);
                uint32_t last = LD_DWORD(&entry[GPT_ENTRY_LAST_LBA_LO]);
                register_partition(first, last - first + 1, /* parttype=*/ 0);  // Not looking up filesystem GUID, will be auto detected
            }
        }
    }
    return p_count;        
}

int Disk :: read_ebr(uint32_t lba)
{
    uint8_t *local_buf = new uint8_t[sector_size];
    Partition *prt;
    uint32_t start, size;
    uint8_t *tbl;

    if(dev->read(local_buf, lba, 1) != RES_OK) {
        delete[] local_buf;
        return -2; // partition unreadable
    }
    
    if(LD_WORD(local_buf + BS_Signature) != 0xAA55) {
        delete[] local_buf;
        return -3; // partition unformatted 
    }
            
    tbl = &local_buf[MBR_PTable];
    dump_hex_relative(tbl, 66);

    for(int p=0;p<4;p++) {
        uint8_t parttype = tbl[4];
        if(parttype) {
            start = LD_DWORD(&tbl[8]);
            size  = LD_DWORD(&tbl[12]);
            printf("EBR Start: %d Size: %d Type: %d\n", start, size, parttype);
            if ((parttype == 0x0F) || (parttype == 0x05)) {
                read_ebr(lba + start);
            } else {
                register_partition(lba + start, size, parttype);
            }
        }
        tbl += 16;
    }            
    delete local_buf;
    return 0;
}

void Disk::register_partition(uint32_t start, uint32_t size, uint8_t parttype)
{
    Partition *prt = new Partition(dev, start, size, parttype);
    printf("Partition created! %p\n", prt);
    if (last_partition) {
        last_partition->next_partition = prt;
    } else {
        partition_list = prt;
    }
    last_partition = prt;
    p_count ++;
}
