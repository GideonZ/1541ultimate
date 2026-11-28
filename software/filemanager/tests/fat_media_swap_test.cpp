// Media swaps on a FileDevice backed by FAT, with the real FileDevice, Disk,
// Partition and FileSystemFAT code and FatFs on a RAM block device.
#include "../../io/usb/tests/host_test/host_test.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../file_device.h"
#include "../filemanager.h"
#include "../../filesystem/embedded_fs.h"
#include "../../filesystem/blockdev_ram.h"
#include "../../filesystem/chanfat_manager.h"
#include "../../filesystem/filesystem_fat.h"

#include <stdio.h>
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
    Medium other;
    // A mounted volume in slot 0, and dummies in every other slot.
    FileDevice *first = new_device(other);
    first->attach_disk(SECTOR);
    ASSERT_EQ(first->probe(), 1);
    Partition dummies[FF_VOLUMES] = {
        Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1),
        Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1),
        Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1),
        Partition(m.blk, 0, SECTORS, 1) };
    for (int i = 1; i < FF_VOLUMES; i++) {
        ASSERT_EQ(ChanFATManager::getManager()->addDrive(&dummies[i]), i);
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

    // The volumes that were there are untouched, and the mounted one still works.
    EXPECT_EQ(used_slots(), FF_VOLUMES);
    for (int i = 1; i < FF_VOLUMES; i++) {
        EXPECT_TRUE(ChanFATManager::getPartition(i) == &dummies[i]);
    }
    Directory *d = NULL;
    EXPECT_EQ(first->get_file_info()->fs->dir_open("/", &d), FR_OK);
    delete d;
    for (int i = 1; i < FF_VOLUMES; i++) {
        ChanFATManager::getManager()->removeDrive(i);
    }
    delete first;
}

// Nothing was ever open, or everything was closed again: the swap frees the file system and its slot.
TEST(FatMediaSwap, DetachAfterFilesWereOpenedAndClosedReleasesTheSlot)
{
    Medium m;
    FileDevice *dev = new_device(m);
    for (int i = 0; i < 2 * FF_VOLUMES; i++) {
        dev->attach_disk(SECTOR);
        ASSERT_EQ(dev->probe(), 1);
        FileSystem *fs = dev->get_file_info()->fs;
        File *f = NULL;
        ASSERT_EQ(fs->file_open("/a.txt", FA_CREATE_ALWAYS | FA_WRITE, &f), FR_OK);
        EXPECT_EQ(f->close(), FR_OK);
        Directory *d = NULL;
        ASSERT_EQ(fs->dir_open("/", &d), FR_OK);
        delete d;
        dev->detach_disk();
        EXPECT_EQ(used_slots(), 0);
    }
    delete dev;
}

// With a file open, the file system is kept (the handle may be in use by another task),
// and the handle fails instead of reaching into freed memory.
TEST(FatMediaSwap, DetachWithAnOpenFileKeepsTheFileSystem)
{
    Medium m;
    FileDevice *dev = new_device(m);
    dev->attach_disk(SECTOR);
    ASSERT_EQ(dev->probe(), 1);
    File *f = NULL;
    ASSERT_EQ(dev->get_file_info()->fs->file_open("/a.txt", FA_CREATE_ALWAYS | FA_WRITE, &f), FR_OK);
    dev->detach_disk();
    EXPECT_EQ(used_slots(), 1);
    EXPECT_TRUE(dev->get_file_info()->fs == NULL);
    uint32_t n = 0;
    EXPECT_FALSE(f->isValid());
    EXPECT_NE(f->write("x", 1, &n), FR_OK);
    f->close();
    EXPECT_EQ(used_slots(), 1); // still kept: closing the handle does not free it
    delete dev;
}

TEST(FatMediaSwap, DetachWithAnOpenDirectoryKeepsTheFileSystem)
{
    Medium m;
    FileDevice *dev = new_device(m);
    dev->attach_disk(SECTOR);
    ASSERT_EQ(dev->probe(), 1);
    Directory *d = NULL;
    ASSERT_EQ(dev->get_file_info()->fs->dir_open("/", &d), FR_OK);
    dev->detach_disk();
    EXPECT_EQ(used_slots(), 1);
    FileInfo fi(32);
    EXPECT_NE(d->get_entry(fi), FR_OK);
    delete d;
    delete dev;
}

// Destroying the device while a file is open follows the same rule as a swap.
TEST(FatMediaSwap, DestroyingTheDeviceWithAnOpenFileFailsTheHandleCleanly)
{
    Medium m;
    FileDevice *dev = new_device(m);
    dev->attach_disk(SECTOR);
    ASSERT_EQ(dev->probe(), 1);
    File *f = NULL;
    ASSERT_EQ(dev->get_file_info()->fs->file_open("/a.txt", FA_CREATE_ALWAYS | FA_WRITE, &f), FR_OK);
    delete dev;
    uint32_t n = 0;
    EXPECT_FALSE(f->isValid());
    EXPECT_NE(f->write("x", 1, &n), FR_OK);
    f->close();
}

// Several handles, some already closed: every one still open is cut loose, none that was closed is touched.
TEST(FatMediaSwap, DetachWithSeveralHandlesCutsEveryOpenOneLoose)
{
    Medium m;
    FileDevice *dev = new_device(m);
    dev->attach_disk(SECTOR);
    ASSERT_EQ(dev->probe(), 1);
    FileSystem *fs = dev->get_file_info()->fs;
    File *f[4];
    char name[8];
    for (int i = 0; i < 4; i++) {
        sprintf(name, "/f%d", i);
        ASSERT_EQ(fs->file_open(name, FA_CREATE_ALWAYS | FA_WRITE, &f[i]), FR_OK);
    }
    f[1]->close();
    f[3]->close();
    Directory *d0 = NULL, *d1 = NULL;
    ASSERT_EQ(fs->dir_open("/", &d0), FR_OK);
    ASSERT_EQ(fs->dir_open("/", &d1), FR_OK);
    delete d0;

    dev->detach_disk();

    uint32_t n = 0;
    EXPECT_FALSE(f[0]->isValid());
    EXPECT_FALSE(f[2]->isValid());
    EXPECT_NE(f[0]->write("x", 1, &n), FR_OK);
    EXPECT_NE(f[2]->write("x", 1, &n), FR_OK);
    f[0]->close();
    f[2]->close();
    FileInfo fi(32);
    EXPECT_NE(d1->get_entry(fi), FR_OK);
    delete d1;
    delete dev;
}

// A file system object without a volume must not format volume 0 through an empty prefix.
TEST(FatMediaSwap, FormatWithNoFreeVolumeFailsAndLeavesVolume0Alone)
{
    Medium m;
    Medium other;
    FileDevice *first = new_device(other);
    first->attach_disk(SECTOR);
    ASSERT_EQ(first->probe(), 1);
    File *f = NULL;
    ASSERT_EQ(first->get_file_info()->fs->file_open("/keep.txt", FA_CREATE_ALWAYS | FA_WRITE, &f), FR_OK);
    ASSERT_EQ(f->close(), FR_OK);
    Partition dummies[FF_VOLUMES] = {
        Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1),
        Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1),
        Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1), Partition(m.blk, 0, SECTORS, 1),
        Partition(m.blk, 0, SECTORS, 1) };
    for (int i = 1; i < FF_VOLUMES; i++) {
        ASSERT_EQ(ChanFATManager::getManager()->addDrive(&dummies[i]), i);
    }

    Partition prt(m.blk, 0, SECTORS, 1);
    FileSystemFAT *fs = new FileSystemFAT(&prt);
    EXPECT_NE(fs->format("OOPS"), FR_OK);
    delete fs;

    File *again = NULL;
    EXPECT_EQ(first->get_file_info()->fs->file_open("/keep.txt", FA_READ, &again), FR_OK);
    if (again) {
        again->close();
    }
    for (int i = 1; i < FF_VOLUMES; i++) {
        ChanFATManager::getManager()->removeDrive(i);
    }
    delete first;
}

// A disk image entered on the medium is held open by the file manager's mount cache.
// SD removal used to detach the disk before invalidating, so the open image kept the file
// system and its volume slot for good. This is the order the callers must use.
static void swap_with_mounted_image(int cycles, int *mounted_out)
{
    Medium m;
    FileDevice *dev = new_device(m);
    FileManager *fm = FileManager::getFileManager();
    fm->add_root_entry(dev); // as the SD card and USB devices are
    int mounted = 0;
    for (int i = 0; i < cycles; i++) {
        dev->attach_disk(SECTOR);
        if (dev->probe() != 1) {
            break;
        }
        mounted++;
        File *img = NULL;
        mstring dev_path;
        dev->get_full_path(dev_path);
        if (dev->get_file_info()->fs->file_open("/img.d64", FA_CREATE_ALWAYS | FA_WRITE | FA_READ, &img) != FR_OK) {
            break;
        }
        img->get_path_reference() = dev_path;
        img->get_path_reference() += "img.d64";
        PathInfo pi(NULL);
        pi.workPath.cd(img->get_path());
        pi.index = pi.workPath.getDepth();
        SubPath &sp = *pi.getSubPath();
        fm->add_mount_point(&sp, img, new FileSystemInFile());
        fm->invalidate(dev); // closes the mounted image, so the detach can free the file system
        dev->detach_disk();
    }
    *mounted_out = mounted;
    fm->remove_root_entry(dev);
    delete dev;
}

TEST(FatMediaSwap, InvalidateBeforeDetachReleasesTheSlotOfAnEnteredImage)
{
    int mounted = 0;
    const int cycles = 2 * FF_VOLUMES;
    swap_with_mounted_image(cycles, &mounted);
    EXPECT_EQ(mounted, cycles);
    EXPECT_EQ(used_slots(), 0);
}
