// SD card removal with a disk image entered, through the real SdCardManager::poll().
// Only the SdCard method bodies are replaced, by a RAM medium; the file device, FAT code,
// file manager and sdcard_manager.cc are the production ones.
#include "../../io/usb/tests/host_test/host_test.h"
#include "../filemanager.h"
#include "../file_device.h"
#include "../../io/sd_card/sd_card.h"
#include "../../io/sd_card/sdcard_manager.h"
#include "../../filesystem/embedded_fs.h"
#include "../../filesystem/chanfat_manager.h"
#include "../../filesystem/filesystem_fat.h"
#include "../../filesystem/partition.h"
#include "../../filesystem/blockdev_ram.h"

#include <string.h>
#include <vector>

extern "C" void dump_hex_relative(const void *, int) { }

namespace {
const int SECTORS = 2048;
uint8_t *medium(void)
{
    static std::vector<uint8_t> m(512 * SECTORS, 0);
    return &m[0];
}
int card_detect = 0;

int used_slots()
{
    int n = 0;
    for (int i = 0; i < FF_VOLUMES; i++) {
        if (ChanFATManager::getPartition(i)) {
            n++;
        }
    }
    return n;
}
} // namespace

// The real SdCard declaration, with a RAM medium behind it.
SdCard::SdCard() { initialized = true; sdhc = false; sd_type = 0; }
SdCard::~SdCard() { }
DSTATUS SdCard::init(void) { return 0; }
DSTATUS SdCard::status(void) { return 0; }
DRESULT SdCard::read(uint8_t *b, uint32_t s, int n) { memcpy(b, medium() + s * 512, n * 512); return RES_OK; }
DRESULT SdCard::write(const uint8_t *b, uint32_t s, int n) { memcpy(medium() + s * 512, b, n * 512); return RES_OK; }
DRESULT SdCard::ioctl(uint8_t c, void *d)
{
    if (c == GET_SECTOR_COUNT) {
        *(uint32_t *)d = SECTORS;
    } else if (c == GET_SECTOR_SIZE) {
        *(uint32_t *)d = 512;
    }
    return RES_OK;
}
int sdio_sense(void) { return card_detect; }

// A disk image entered on the card is held open by the file manager's mount cache. Removing
// the card must close it before the disk is detached, or the file system keeps its volume
// slot for good and the 11th insertion finds none.
TEST(SdPoll, RemovalWithAnEnteredImageReleasesTheVolumeSlot)
{
    {
        BlockDevice_Ram ram(medium(), 512, SECTORS);
        Partition prt(&ram, 0, SECTORS, 1);
        FileSystemFAT fs(&prt);
        ASSERT_EQ(fs.format("SD"), FR_OK);
    }
    FileManager *fm = FileManager::getFileManager();
    const int cycles = 2 * FF_VOLUMES;
    int mounted = 0;
    for (int i = 0; i < cycles; i++) {
        card_detect = SD_CARD_DETECT;
        sd_card_manager.poll(); // from no media: card not ready
        sd_card_manager.poll(); // card ready, disk attached (the first cycle starts from the boot state)
        File *img = NULL;
        if (fm->fopen("/SD/img.d64", FA_CREATE_ALWAYS | FA_WRITE | FA_READ, &img) != FR_OK) {
            break;
        }
        mounted++;
        // Enter the image as the file manager does: the mount cache owns the open file.
        PathInfo pi(NULL);
        pi.workPath.cd(img->get_path());
        pi.index = pi.workPath.getDepth();
        fm->add_mount_point(pi.getSubPath(), img, new FileSystemInFile());

        card_detect = 0;
        sd_card_manager.poll(); // removal
    }
    EXPECT_EQ(mounted, cycles);
    EXPECT_EQ(used_slots(), 0);
}
