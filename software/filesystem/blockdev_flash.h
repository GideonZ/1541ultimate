#ifndef BLOCKDEV_RAM_H
#define BLOCKDEV_RAM_H

#include "blockdev.h"
#include "file_system.h"
#include "file.h"
#include "flash.h"

class BlockDevice_Flash : public BlockDevice
{
    Flash *chip;
    t_flash_address addr;
    int number_of_sectors;
    int page_size;
    int sector_size;
    int pages_per_sector;
    int first_sector;
    int first_page;
    bool requires_erase;
public:
    BlockDevice_Flash(Flash *);
    ~BlockDevice_Flash();

    virtual DSTATUS init(void);
    virtual DSTATUS status(void);
    virtual DRESULT read(uint8_t *, uint32_t, int);
    virtual DRESULT write(const uint8_t *, uint32_t, int);
    virtual DRESULT ioctl(uint8_t, void *);
};

void reformat_flash_disk(void);
void init_flash_disk(void);

#endif
