
extern "C" {
    #include "small_printf.h"
}
#include "blockdev_file.h"
#include "filemanager.h"

BlockDevice_File::BlockDevice_File(PathObject *obj, int sec_size)
{
	FileInfo *f = obj->get_file_info();
    shift = 0;
    while(sec_size > 1) {
        shift++;
        sec_size >>= 1;
    }
    if(shift > 12) {
        shift = 12;
    }
    sector_size = (1 << shift);
    file_size = f->size;
	file = root.fopen(obj, FA_READ); // TODO: | FA_WRITE
}
    
BlockDevice_File::~BlockDevice_File()
{
	root.fclose(file);
}

DSTATUS BlockDevice_File::init(void)
{
    // We don't need to do anything here, just return status
    return status();
}
        
DSTATUS BlockDevice_File::status(void)
{
    if(file)
        return 0;
    else
        return STA_NODISK;
}
    
DRESULT BlockDevice_File::read(BYTE *buffer, DWORD sector, BYTE count)
{
//    printf("BlockDevice_File read sector: %d.\n", sector);

    if(file->seek(sector << shift))
        return RES_PARERR;
        
    UINT read;
    if(file->read(buffer, sector_size * (int)count, &read) != FR_OK)
        return RES_ERROR;
    
//    printf("Read ok!\n");
    return RES_OK;
}

DRESULT BlockDevice_File::write(const BYTE *buffer, DWORD sector, BYTE count)
{
    if(file->seek(sector << shift))
        return RES_PARERR;
        
    UINT written;
    if(file->write((BYTE *)buffer, sector_size * (int)count, &written) != FR_OK)
        return RES_ERROR;
    
    return RES_OK;
}

DRESULT BlockDevice_File::ioctl(BYTE command, void *data)
{
    DWORD size;
    DWORD *dest = (DWORD *)data;
    
    switch(command) {
        case GET_SECTOR_COUNT:
            if(!file)
                return RES_ERROR;
            size = file_size >> shift;
            *dest = size;
            break;
        case GET_SECTOR_SIZE:
            (*(DWORD *)data) = sector_size;
            break;
        case GET_BLOCK_SIZE:
            (*(DWORD *)data) = 128*1024;
            break;
        default:
            printf("IOCTL %d.\n", command);
            return RES_PARERR;
    }
    return RES_OK;
}
