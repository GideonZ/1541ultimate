// Media swaps on a FileDevice backed by FAT, with the real FileDevice, Disk,
// Partition and FileSystemFAT code and FatFs on a RAM block device.
#include "../../io/usb/tests/host_test/host_test.h"
#include "../file_device.h"
#include "../../filesystem/blockdev_ram.h"
#include "../../filesystem/chanfat_manager.h"
#include "../../filesystem/filesystem_fat.h"

#include <string.h>
#include <vector>

// Disk prints extended partition tables with this; nothing to show on the host.
extern "C" void dump_hex_relative(const void *, int) { }

namespace {

const int SECTOR = 512;
const int SECTORS = 2048;

// Number of FatFs volume slots that are in use.
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

// A failing test must not leave volume slots behind for the next one.
void reset_slots()
{
    for (int i = 0; i < FF_VOLUMES; i++) {
        ChanFATManager::getManager()->removeDrive(i);
    }
}

struct Medium {
    std::vector<uint8_t> memory;
    BlockDevice_Ram *blk;

    Medium() : memory(SECTOR * SECTORS, 0)
    {
        reset_slots();
        blk = new BlockDevice_Ram(&memory[0], SECTOR, SECTORS);
        Partition prt(blk, 0, SECTORS, 1);
        FileSystemFAT *fs = new FileSystemFAT(&prt);
        FRESULT res = fs->format("TEST");
        delete fs;
        if (res != FR_OK) {
            throw host_test::TestFailure("format of the RAM medium failed");
        }
    }
    ~Medium() { delete blk; }
};

// A file device on the medium, as a removable drive: one volume, no partition table.
FileDevice *new_device(Medium &m)
{
    FileDevice *dev = new FileDevice(m.blk, "Test", "Test");
    dev->setFloppy();
    return dev;
}

} // namespace

TEST(FatMediaSwap, EveryAttachedFileSystemIsReleasedByDetach)
{
    Medium m;
    FileDevice *dev = new_device(m);
    int mounted = 0;
    const int cycles = 3 * FF_VOLUMES;
    for (int i = 0; i < cycles; i++) {
        dev->attach_disk(SECTOR);
        if (dev->probe() == 1) {
            mounted++;
        }
        EXPECT_EQ(used_slots(), 1);
        dev->detach_disk();
        EXPECT_EQ(used_slots(), 0);
        EXPECT_TRUE(dev->get_file_info()->fs == NULL);
    }
    EXPECT_EQ(mounted, cycles);
    delete dev;
    EXPECT_EQ(used_slots(), 0);
}

TEST(FatMediaSwap, FileOpenAcrossDetachFailsCleanly)
{
    Medium m;
    FileDevice *dev = new_device(m);
    dev->attach_disk(SECTOR);
    ASSERT_EQ(dev->probe(), 1);
    FileSystem *fs = dev->get_file_info()->fs;
    File *f = NULL;
    ASSERT_EQ(fs->file_open("/a.txt", FA_CREATE_ALWAYS | FA_WRITE | FA_READ, &f), FR_OK);
    uint32_t n = 0;
    ASSERT_EQ(f->write("hello", 5, &n), FR_OK);

    dev->detach_disk(); // the medium is gone; the caller still holds the handle

    char buf[8];
    EXPECT_FALSE(f->isValid());
    EXPECT_NE(f->read(buf, 5, &n), FR_OK);
    EXPECT_NE(f->write("x", 1, &n), FR_OK);
    EXPECT_NE(f->seek(0), FR_OK);
    EXPECT_NE(f->sync(), FR_OK);
    f->close();

    dev->attach_disk(SECTOR);
    ASSERT_EQ(dev->probe(), 1);
    delete dev;
}

TEST(FatMediaSwap, DirectoryOpenAcrossDetachFailsCleanly)
{
    Medium m;
    FileDevice *dev = new_device(m);
    dev->attach_disk(SECTOR);
    ASSERT_EQ(dev->probe(), 1);
    Directory *d = NULL;
    ASSERT_EQ(dev->get_file_info()->fs->dir_open("/", &d), FR_OK);

    dev->detach_disk();

    FileInfo fi(32);
    EXPECT_NE(d->get_entry(fi), FR_OK);
    delete d;
    delete dev;
}

TEST(FatMediaSwap, FailedMountWithNoFreeVolumeKeepsOtherVolumes)
{
    Medium m;
    // Occupy every slot with something that is not ours.
    Partition others[FF_VOLUMES] = {
        Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1),
        Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1),
        Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1),
        Partition(m.blk, 0, SECTORS, 1) };
    for (int i = 0; i < FF_VOLUMES; i++) {
        ASSERT_EQ(ChanFATManager::getManager()->addDrive(&others[i]), i);
    }
    // Leave zeroes where the next file system object will be allocated.
    void *poison = ::operator new(sizeof(FileSystemFAT));
    memset(poison, 0, sizeof(FileSystemFAT));
    ::operator delete(poison);

    FileDevice *dev = new_device(m);
    dev->attach_disk(SECTOR);
    EXPECT_NE(dev->probe(), 1); // no volume left
    dev->detach_disk();
    delete dev;

    EXPECT_EQ(used_slots(), FF_VOLUMES);
    for (int i = 0; i < FF_VOLUMES; i++) {
        EXPECT_TRUE(ChanFATManager::getPartition(i) == &others[i]);
        ChanFATManager::getManager()->removeDrive(i);
    }
}
