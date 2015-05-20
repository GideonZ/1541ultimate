#include "iso9660.h"
#include "dump_hex.h"
#include <ctype.h>

FileSystem_ISO9660 :: FileSystem_ISO9660(Partition *p) : FileSystem(p)
{
    initialized = false;
    prt = p;
    DRESULT status = prt->ioctl(GET_SECTOR_SIZE, &sector_size);
    sector_buffer = NULL;
    if(status == 0) {
        if(sector_size <= 4096) { // we shouldn't try to allocate more
            sector_buffer = new uint8_t[sector_size];
        }
    }
    joliet = false;
    last_read_sector = 0;
    root_dir_sector = 0;
    root_dir_size = 0;
}

FileSystem_ISO9660 :: ~FileSystem_ISO9660()
{
    if(sector_buffer)
        delete sector_buffer;
}

void FileSystem_ISO9660 :: get_dir_record(void *p)
{
    uint8_t *buf = (uint8_t *)p;
    if(buf[0] == 0) {
        dir_record.actual.record_length = 0;
        return;
    }
    memcpy(&dir_record.actual, p, int(buf[0]));
    // end identifier with \0:
    uint8_t ident_len = dir_record.actual.identifier_length;
    dir_record.identifier[ident_len] = 0;
}
    
FileSystem* FileSystem_ISO9660 :: test(Partition *p)        // check if file system is present on this partition
{
    uint32_t secsize;
    DRESULT status = p->ioctl(GET_SECTOR_SIZE, &secsize);
    uint8_t *buf;
    if(status)
        return NULL;
    if(secsize > 4096)
        return NULL;
    if(secsize < 512)
        return NULL;
        
    buf = new uint8_t[secsize];

    // the first 32K are unused.
    uint32_t first_sector = 32768 / secsize;
    status = p->read(buf, first_sector, 1);
    if(status) {
        delete buf;
        return NULL;
    }
    // check for volume descriptor
    t_iso9660_volume_descriptor *volume = (t_iso9660_volume_descriptor *)buf;

    if(memcmp(volume->key, c_volume_key, 8)) {
        delete buf;
        return NULL;
    }
    printf("Correct volume key!");
    delete buf;
    return new FileSystem_ISO9660(p);
}

bool    FileSystem_ISO9660 :: init(void)              // Initialize file system
{
    initialized = false;
    joliet = false;
    
    uint32_t first_sector = 32768 / sector_size;
    DRESULT status = prt->read(sector_buffer, first_sector, 1);
    last_read_sector = first_sector;
    if(status) {
        return false;
    }
    //dump_hex(sector_buffer, 200);
    
    t_iso9660_volume_descriptor *volume = (t_iso9660_volume_descriptor *)sector_buffer;
    if(memcmp(volume->key, c_volume_key, 8)) {
        return false;
    }
    printf("ISO9660 Information:\n");
    volume->volume_identifier[31] = 0;
    printf("Volume name: %s\n", volume->volume_identifier);
    printf("Sector size: %d\n", volume->sector_size);
    printf("Sector_count: %d\n", volume->sector_count);
    get_dir_record(&volume->root_dir_rec);
    
    root_dir_sector = dir_record.actual.sector;
    root_dir_size   = dir_record.actual.file_size;
    printf("Root directory located at sector %d. (length = %d)\n", root_dir_sector, root_dir_size);
    initialized = true;

    // attempt Joliet
    do {
        first_sector ++;
        DRESULT status = prt->read(sector_buffer, first_sector, 1);
        last_read_sector = first_sector;
        if(status) {
            return true; // accept ISO9660 only, eventhough this is very weird
        }
        if((volume->key[0] == 0xFF) || (volume->key[0] == 0x00)) {
            return true; // accept ISO9660 only. no Joliet information found
        }
        if(volume->key[0] == 0x02)
            break;
    } while(1);    

    // Joliet found: process
    joliet = true;
    printf("Joliet descriptor found..\n");
    get_dir_record(&volume->root_dir_rec);
    root_dir_sector = dir_record.actual.sector;
    root_dir_size   = dir_record.actual.file_size;
    printf("Joliet directory located at sector %d. (length = %d)\n", root_dir_sector, root_dir_size);
    
    return true;
}

// functions for reading directories
Directory *FileSystem_ISO9660 :: dir_open(FileInfo *info) // Opens directory (creates dir object, NULL = root)
{
    t_iso_handle *handle = new t_iso_handle;
    if(info && info->cluster) {
        handle->sector = info->cluster;
        handle->remaining = info->size;
    }
    else {
        handle->sector = root_dir_sector;
        handle->remaining = root_dir_size;
    }

    handle->start  = handle->sector;
    handle->offset = 0;
    Directory *dir = new Directory(this, (uint32_t)handle);
    return dir;    
}

void FileSystem_ISO9660 :: dir_close(Directory *d)    // Closes (and destructs dir object)
{
    t_iso_handle *handle = (t_iso_handle *)d->handle;
    delete handle;
    delete d;
}

FRESULT FileSystem_ISO9660 :: dir_read(Directory *d, FileInfo *f) // reads next entry from dir
{
    t_iso_handle *handle = (t_iso_handle *)d->handle;

try_next:
//	printf("Handle: Sector: %d. Offset: %d Remaining: %d\n", handle->sector, handle->offset, handle->remaining);

	if (handle->remaining <= handle->offset) {
		return FR_NO_FILE;
	}

	if(last_read_sector != handle->sector) {
        DRESULT status = prt->read(sector_buffer, handle->sector, 1);
        //dump_hex(sector_buffer, sector_size);
        last_read_sector = handle->sector;
    }
	get_dir_record(&sector_buffer[handle->offset]);

    if((!dir_record.actual.record_length)||(handle->offset >= sector_size)) {
        // lets try the following sector, in case offset != 0
        if (handle->offset == 0)
            return FR_NO_FILE;
        handle->sector ++;
        handle->offset = 0;
        handle->remaining -= sector_size;
		goto try_next;
    }

    // skip . and ..
    if(dir_record.actual.identifier_length == 1) {
        if((dir_record.identifier[0] == 0) || (dir_record.identifier[0] == 1)) {
            handle->offset += int(dir_record.actual.record_length);
            goto try_next;
        }
    }

    f->cluster    = dir_record.actual.sector;
    f->dir_clust  = handle->start;
    f->attrib     = (dir_record.actual.flags & 0x02)?(AM_DIR):0;
    f->fs         = this;
    f->size       = dir_record.actual.file_size;
    // get filename
    int ident_len = int(dir_record.actual.identifier_length);
    char *ident = dir_record.identifier;
    char *dest = f->lfname;
    if(joliet) {
        ident_len >>= 1; // copy only half of the chars
    }
    if(ident_len >= f->lfsize)  // truncate
        ident_len = f->lfsize-1;
    for(int i=0;i<ident_len;i++) {
        if(joliet)
            ident++;
        *(dest++) = *(ident++);
    }
    *(dest) = 0;

    // eliminate version string ;1
    if(!(f->attrib & AM_DIR)) {
        while(dest != f->lfname) {
            if(*(--dest) == ';') {
                *dest = 0;
                break;
            }
        }    
    }
    // get extension
    f->extension[0] = 0;
    ident_len = int(dir_record.actual.identifier_length);
    int pos = ident_len-1;
    ident = &(dir_record.identifier[ident_len-1]);
    while(pos > 0) {
        if(dir_record.identifier[pos] == '.') {
            for(int i=0;i<3;i++) {
                pos += (joliet)?2:1;
                if(pos < ident_len)
                    f->extension[i] = toupper(dir_record.identifier[pos]);
                else
                    f->extension[i] = 0;
            }
            break;
        }
        pos -= (joliet)?2:1;
    }            
    f->extension[3] = 0;
    
    // move to next
    handle->offset += int(dir_record.actual.record_length);

    return FR_OK;
}

// functions for reading files
File   *FileSystem_ISO9660 :: file_open(FileInfo *info, uint8_t flags)  // Opens file (creates file object)
{
//    if(flags & FA_ANY_WRITE_FLAG)
//        return NULL;

    t_iso_handle *handle = new t_iso_handle;
    handle->sector    = info->cluster;
    handle->remaining = info->size;
    handle->start     = handle->sector;
    handle->offset    = 0;

    File *f = new File(info, (uint32_t)handle);
    return f;    
}

void    FileSystem_ISO9660 :: file_close(File *f)                // Closes file (and destructs file object)
{
    t_iso_handle *handle = (t_iso_handle *)f->handle;
    delete handle;
    delete f;
}

FRESULT FileSystem_ISO9660 :: file_read(File *f, void *buffer, uint32_t len, UINT *transferred)
{
    t_iso_handle *handle = (t_iso_handle *)f->handle;
    
    uint32_t file_remain = handle->remaining - handle->offset;
    if (len > file_remain)
        len = file_remain;

    uint8_t *dest = (uint8_t *)buffer;

    // calculate which sector to start reading
    uint32_t sect        = (handle->offset / sector_size);
    uint32_t sect_offset = handle->offset - (sector_size * sect);
    sect += handle->start;

//    printf("File read: Offset = %d, Len = %d. Sect = %d. Sect_offset = %d.\n", handle->offset, len, sect, sect_offset);
    // perform reads
    *transferred = 0;
    DSTATUS res; 
    while(len) {
        if((sect_offset == 0) && (len >= sector_size)) { // optimized read, directly to buffer
            res = prt->read(dest, sect, 1);
//            printf("ISO9660: Read sector direct: %d.\n", sect);
            if(!res) { // ok
                sect ++;
                dest += sector_size;
                len -= sector_size;
                handle->offset += sector_size;
                *transferred += sector_size;
            } else {
                return FR_DISK_ERR;
            }
        } else { // unaligned read
            // make sure the right sector is in our buffer
            if(last_read_sector != sect) {
                res = prt->read(sector_buffer, sect, 1);
//                printf("ISO9660: Read sector to buffer: %d.\n", sect);
                if(!res)
                    last_read_sector = sect;
                else
                    return FR_DISK_ERR;
            }
            uint32_t sect_remain = sector_size - sect_offset;
            uint32_t take_now = sect_remain;
            if(sect_remain > len)
                take_now = len;
//            printf("ISO9660: Taking now %d bytes from buffer offset %d.\n", take_now, sect_offset);
            memcpy(dest, &sector_buffer[sect_offset], take_now);
            dest += take_now;
            len -= take_now;
            handle->offset += take_now;
            *transferred += take_now;
            // are we at the end of the sector now?
            if(sect_remain == take_now) {
                sect_offset = 0;
                sect ++;
            } else {
                sect_offset += take_now;
            }
        }
    }
    return FR_OK;
}

FRESULT FileSystem_ISO9660 :: file_seek(File *f, uint32_t pos)
{
    t_iso_handle *handle = (t_iso_handle *)f->handle;

    if(pos >= handle->remaining)
        handle->offset = handle->remaining;
    else
        handle->offset = pos;

    return FR_OK;        
}

//FileSystemRegistrator iso_tester(FileSystem_ISO9660 :: test);

FactoryRegistrator<Partition *, FileSystem *> iso_tester(file_system_factory, FileSystem_ISO9660 :: test);
