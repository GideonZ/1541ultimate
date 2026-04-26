
extern "C" {
    #include "small_printf.h"
}
#include "blockdev_flash.h"
#include "filemanager.h"
#include "file_device.h"
#include "filesystem_fat.h"
#include "init_function.h"
#include "dump_hex.h"
#include "u64.h"
#include "c64.h"

BlockDevice_Flash::BlockDevice_Flash(Flash *flash)
{
    chip = flash;
    flash->get_image_addresses(FLASH_ID_FLASHDRIVE, &addr);
	page_size = flash->get_page_size();
	if (page_size == 528) { // AT45 flash
	    sector_size = 512;
	    pages_per_sector = 1;
	    first_page = addr.device_addr >> 10;
	    first_sector = first_page;
	} else { // all other flash
        sector_size = flash->get_sector_size(addr.start);
        pages_per_sector = sector_size / page_size;
        first_page = addr.start / page_size;
        first_sector = flash->page_to_sector(first_page);
	}

    requires_erase = flash->need_erase();
	if (addr.id == FLASH_ID_FLASHDRIVE) {
		number_of_sectors = addr.max_length / sector_size;
        set_state(e_device_ready);
    } else {
    	number_of_sectors = 0;
        set_state(e_device_no_media);
    }
    //printf("BlockDevice Flash:\nSectorSize: %d\nPagesPerSector: %d\nFirstPage: %d\nFirstSector: %d\nErase:%d\nNumberOfSectors: %d\n",
    //        sector_size, pages_per_sector, first_page, first_sector, requires_erase, number_of_sectors);
}
    
BlockDevice_Flash::~BlockDevice_Flash()
{
}

DSTATUS BlockDevice_Flash::init(void)
{
    // We don't need to do anything here, just return status
    return status();
}
        
DSTATUS BlockDevice_Flash::status(void)
{

	if(number_of_sectors)
        return 0;
    else
        return STA_NODISK;
}
    
DRESULT BlockDevice_Flash::read(uint8_t *buffer, uint32_t sector, int count)
{
    if(sector >= number_of_sectors)
        return RES_PARERR;
    
    int page = first_page + (sector * pages_per_sector);
    sector += first_sector;
    for(int i=0; i < pages_per_sector * count; i++) {
        if (!chip->read_page_power2(page, buffer)) {
            printf("Read Error\n");
            return RES_ERROR;
        }
        buffer += page_size;
        page++;
    }
    return RES_OK;
}

DRESULT BlockDevice_Flash::write(const uint8_t *buffer, uint32_t sector, int count)
{
	if(sector >= number_of_sectors)
        return RES_PARERR;
        
    int page = first_page + (sector * pages_per_sector);
    sector += first_sector;
    if (requires_erase) {
        if(!chip->erase_sector(sector)) {
            printf("Erase Error\n");
            return RES_ERROR;
        }
    }
    for(int i=0; i < pages_per_sector * count; i++) {
    	if (!chip->write_page(page, buffer)) {
            printf("Write Error\n");
    		return RES_ERROR;
    	}
    	buffer += page_size;
    	page++;
    }
    return RES_OK;
}

DRESULT BlockDevice_Flash::ioctl(uint8_t command, void *data)
{
    uint32_t size;
    uint32_t *dest = (uint32_t *)data;
    
    switch(command) {
        case GET_SECTOR_COUNT:
            if(!number_of_sectors)
                return RES_ERROR;
            *dest = number_of_sectors;
            break;
        case GET_SECTOR_SIZE:
            if(!number_of_sectors)
                return RES_ERROR;
            *dest = sector_size;
            break;
        case GET_BLOCK_SIZE:
            if(!number_of_sectors)
                return RES_ERROR;
            *dest = 1; // erase size is 1 sector
            break;
        case CTRL_SYNC:
            break;
        default:
            printf("IOCTL %d.\n", command);
            return RES_PARERR;
    }
    return RES_OK;
}

BlockDevice *flashdisk_blk;
FileDevice *flashdisk_node;

void format_flash(void)
{
    FileSystem *flashdisk_fs;
    Partition *flashdisk_prt;
    flashdisk_prt = new Partition(flashdisk_blk, 0, 0, 0);
    flashdisk_fs  = new FileSystemFAT(flashdisk_prt);
    flashdisk_fs->format("FlashDisk");
    delete flashdisk_fs;
    delete flashdisk_prt;
}

void init_flash_disk(void)
{
    static bool initialized = false;
    if (initialized) {
        return;
    }

    Flash *flash = get_flash();
    if (!flash) {
        return;
    }

#if U64
    if (U64_RESTORE_REG == 1) {
        return;
    }
#else
    if ((ioRead8(ITU_BUTTON_REG) & ITU_BUTTON0) != 0) {
        return;
    }
#endif

    flashdisk_blk = new BlockDevice_Flash(flash);

    flashdisk_node = new FileDevice(flashdisk_blk, "Flash", "Flash Disk");
    flashdisk_node->attach_disk(flash->get_sector_size(0));
    int a = flashdisk_node->probe();
    if (a < 1) {
    	format_flash();
        a = flashdisk_node->probe();
    }
    if (a > 0) {
    	FileManager :: getFileManager()->add_root_entry(flashdisk_node);
    }
    initialized = true;
}

void reformat_flash_disk(void)
{
    Flash *flash = get_flash();
    if (!flash) {
        return;
    }
    flashdisk_node->detach_disk();
    format_flash();
    flashdisk_node->attach_disk(flash->get_sector_size(0));
    flashdisk_node->probe();
}

// InitFunction flashdisk_init("Flash Disk", init_flash_disk, NULL, NULL, 1);

