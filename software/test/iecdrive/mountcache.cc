// Entering a disk image in the file browser leaves the image mounted in the
// FileManager mount cache, holding that image's BAM and directory in memory.
// An emulated drive writes the same image file through its own file handle.
// The cached copy must not be used after that, or the browser allocates blocks
// the drive has already used and overwrites the data the drive wrote.

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "filemanager.h"
#include "embedded_d64.h"
#include "directory.h"
#include "blockdev_emul.h"
#include "filesystem_fat.h"
#include "macros.h"

void create_iec_d64_fixture(const char *path);
void create_file(const char *filename, int blocks);
FRESULT copy_to(const char *from, const char *to);

#define D64_SIZE 174848
#define PAYLOAD_BLOCKS 20
#define PAYLOAD_SIZE (PAYLOAD_BLOCKS * 254)

static void fill_pattern(uint8_t *buffer, int len, uint8_t seed)
{
    for (int i = 0; i < len; i++) {
        buffer[i] = (uint8_t)(seed ^ (i * 7) ^ (i >> 5));
    }
}

// Writes a file into the image through a file system of its own, the way the
// emulated drive reaches the image file: a second handle on the same file.
static void write_through_own_view(const char *testname, FileManager *fm, const char *image,
                                   const char *name, const uint8_t *data, int len)
{
    File *f = NULL;
    REQUIRE(fm->fopen(image, FA_READ | FA_WRITE, &f) == FR_OK);

    FileSystemInFile_D64 emb(0, true);
    emb.init(f);
    FileSystem *fs = emb.getFileSystem();
    REQUIRE(fs != NULL);

    File *inner = NULL;
    REQUIRE(fs->file_open(name, FA_WRITE | FA_CREATE_NEW, &inner) == FR_OK);
    uint32_t transferred = 0;
    REQUIRE(inner->write(data, len, &transferred) == FR_OK);
    REQUIRE((int)transferred == len);
    inner->close();
    REQUIRE(fs->sync() == FR_OK);

    fm->fclose(f);
}

static void list_image(const char *testname, FileManager *fm, const char *image)
{
    Path *p = fm->get_new_path("mountcache");
    p->cd(image);
    IndexedList<FileInfo *> listing(16, NULL);
    REQUIRE(fm->get_directory(p, listing, NULL) == FR_OK);
    for (int i = 0; i < listing.get_elements(); i++) {
        delete listing[i];
    }
    listing.clear_list();
    fm->release_path(p);
}

static void write_through_mount(const char *testname, FileManager *fm, const char *path,
                                const uint8_t *data, int len)
{
    File *f = NULL;
    REQUIRE(fm->fopen(path, FA_WRITE | FA_CREATE_NEW, &f) == FR_OK);
    uint32_t transferred = 0;
    REQUIRE(f->write(data, len, &transferred) == FR_OK);
    REQUIRE((int)transferred == len);
    fm->fclose(f);
}

static void read_image(const char *testname, FileManager *fm, const char *image, uint8_t *buffer, uint32_t size = D64_SIZE)
{
    File *f = NULL;
    REQUIRE(fm->fopen(image, FA_READ, &f) == FR_OK);
    uint32_t transferred = 0;
    REQUIRE(f->read(buffer, size, &transferred) == FR_OK);
    REQUIRE(transferred == size);
    fm->fclose(f);
}

// Number of 254-byte data areas of the image that hold the given block of the
// payload. The drive wrote each block exactly once, so the count is 1 per block
// while the data is intact and 0 once it has been overwritten.
static int count_payload_blocks(const uint8_t *image, const uint8_t *payload)
{
    int found = 0;
    for (int block = 0; block < PAYLOAD_BLOCKS; block++) {
        const uint8_t *wanted = payload + 254 * block;
        for (int sector = 0; sector < D64_SIZE / 256; sector++) {
            if (memcmp(image + 256 * sector + 2, wanted, 254) == 0) {
                found++;
                break;
            }
        }
    }
    return found;
}

static void check_payload_survived(const char *testname, FileManager *fm, const char *image,
                                   const uint8_t *payload, const char *name)
{
    uint8_t *image_data = new uint8_t[D64_SIZE];
    read_image(testname, fm, image, image_data);
    int blocks = count_payload_blocks(image_data, payload);
    printf("%s: %s blocks still on the image: %d of %d\n", testname, name, blocks, PAYLOAD_BLOCKS);

    uint8_t entry[16];
    memset(entry, 0xA0, sizeof(entry));
    memcpy(entry, name, strlen(name));
    bool entry_found = false;
    for (int i = 0; i + (int)sizeof(entry) <= D64_SIZE; i++) {
        if (memcmp(image_data + i, entry, sizeof(entry)) == 0) {
            entry_found = true;
            break;
        }
    }
    printf("%s: %s directory entry: %s\n", testname, name, entry_found ? "present" : "gone");
    delete[] image_data;

    REQUIRE(blocks == PAYLOAD_BLOCKS);
    REQUIRE(entry_found);
}

// The browser enters the image first, then the drive mounts it and writes.
static void browser_first(FileManager *fm)
{
    const char *testname = "Suite12-BrowserFirst";
    printf("\n[%s] Browser lists the image, then a second handle writes it\n", testname);

    const char *image = "/Fat/stale1.d64";
    create_iec_d64_fixture("output/stale1.d64");
    REQUIRE(copy_to("output/stale1.d64", image) == FR_OK);

    list_image(testname, fm, image);

    uint8_t *payload = new uint8_t[PAYLOAD_SIZE];
    fill_pattern(payload, PAYLOAD_SIZE, 0x5A);
    write_through_own_view(testname, fm, image, "DRIVEA", payload, PAYLOAD_SIZE);
    check_payload_survived(testname, fm, image, payload, "DRIVEA");

    uint8_t *other = new uint8_t[PAYLOAD_SIZE];
    fill_pattern(other, PAYLOAD_SIZE, 0xA5);
    write_through_mount(testname, fm, "/Fat/stale1.d64/BROWSERA", other, PAYLOAD_SIZE);

    check_payload_survived(testname, fm, image, payload, "DRIVEA");

    delete[] payload;
    delete[] other;
    printf("[%s] passed\n", testname);
}

// The drive holds the image open, the browser enters it while it is held, and
// the drive writes and closes afterwards.
static void drive_holds_image(FileManager *fm)
{
    const char *testname = "Suite12-DriveHoldsImage";
    printf("\n[%s] Second handle writes the image while the browser holds a listing\n", testname);

    const char *image = "/Fat/stale2.d64";
    create_iec_d64_fixture("output/stale2.d64");
    REQUIRE(copy_to("output/stale2.d64", image) == FR_OK);

    File *drive = NULL;
    REQUIRE(fm->fopen(image, FA_READ | FA_WRITE, &drive) == FR_OK);

    list_image(testname, fm, image);

    FileSystemInFile_D64 emb(0, true);
    emb.init(drive);
    FileSystem *fs = emb.getFileSystem();
    REQUIRE(fs != NULL);

    uint8_t *payload = new uint8_t[PAYLOAD_SIZE];
    fill_pattern(payload, PAYLOAD_SIZE, 0x3C);
    File *inner = NULL;
    REQUIRE(fs->file_open("DRIVEB", FA_WRITE | FA_CREATE_NEW, &inner) == FR_OK);
    uint32_t transferred = 0;
    REQUIRE(inner->write(payload, PAYLOAD_SIZE, &transferred) == FR_OK);
    REQUIRE((int)transferred == PAYLOAD_SIZE);
    inner->close();
    REQUIRE(fs->sync() == FR_OK);
    fm->fclose(drive);

    check_payload_survived(testname, fm, image, payload, "DRIVEB");

    uint8_t *other = new uint8_t[PAYLOAD_SIZE];
    fill_pattern(other, PAYLOAD_SIZE, 0xC3);
    write_through_mount(testname, fm, "/Fat/stale2.d64/BROWSERB", other, PAYLOAD_SIZE);

    check_payload_survived(testname, fm, image, payload, "DRIVEB");

    delete[] payload;
    delete[] other;
    printf("[%s] passed\n", testname);
}

// A second writer owns the image. The browser may read it, but cannot allocate
// from a cached BAM until that writer closes its handle.
static void no_eject(FileManager *fm)
{
    const char *testname = "Suite12-NoEject";
    printf("\n[%s] Browser refuses writes while a second handle holds the image\n", testname);

    const char *image = "/Fat/stale3.d64";
    create_iec_d64_fixture("output/stale3.d64");
    REQUIRE(copy_to("output/stale3.d64", image) == FR_OK);

    list_image(testname, fm, image);

    File *drive = NULL;
    REQUIRE(fm->fopen(image, FA_READ | FA_WRITE, &drive) == FR_OK);

    list_image(testname, fm, image);

    FileSystemInFile_D64 emb(0, true);
    emb.init(drive);
    FileSystem *fs = emb.getFileSystem();
    REQUIRE(fs != NULL);

    uint8_t *payload = new uint8_t[PAYLOAD_SIZE];
    fill_pattern(payload, PAYLOAD_SIZE, 0x69);
    File *inner = NULL;
    REQUIRE(fs->file_open("DRIVEC", FA_WRITE | FA_CREATE_NEW, &inner) == FR_OK);
    uint32_t transferred = 0;
    REQUIRE(inner->write(payload, PAYLOAD_SIZE, &transferred) == FR_OK);
    REQUIRE((int)transferred == PAYLOAD_SIZE);
    inner->close();
    REQUIRE(fs->sync() == FR_OK);

    check_payload_survived(testname, fm, image, payload, "DRIVEC");

    uint8_t *other = new uint8_t[PAYLOAD_SIZE];
    fill_pattern(other, PAYLOAD_SIZE, 0x96);
    File *browser = NULL;
    REQUIRE(fm->fopen("/Fat/stale3.d64/BROWSERC", FA_WRITE | FA_CREATE_NEW, &browser) == FR_WRITE_PROTECTED);
    REQUIRE(browser == NULL);

    check_payload_survived(testname, fm, image, payload, "DRIVEC");

    fm->fclose(drive);
    write_through_mount(testname, fm, "/Fat/stale3.d64/BROWSERC", other, PAYLOAD_SIZE);
    check_payload_survived(testname, fm, image, payload, "DRIVEC");
    delete[] payload;
    delete[] other;
    printf("[%s] passed\n", testname);
}

// The image is deleted and a new one is created under the same path. The
// listing must describe the new image.
static void recreate_same_name(FileManager *fm)
{
    const char *testname = "Suite12-RecreateSameName";
    printf("\n[%s] An image deleted and created again under the same path\n", testname);

    const char *image = "/Fat/stale4.d64";
    create_iec_d64_fixture("output/stale4.d64");
    REQUIRE(copy_to("output/stale4.d64", image) == FR_OK);

    list_image(testname, fm, image);

    uint8_t payload[254];
    fill_pattern(payload, sizeof(payload), 0x7E);
    write_through_mount(testname, fm, "/Fat/stale4.d64/OLDFILE", payload, sizeof(payload));

    REQUIRE(fm->delete_file(image) == FR_OK);

    // A fresh image of the same size at the same path, as creating one in the
    // browser does.
    uint8_t *blank = new uint8_t[D64_SIZE];
    memset(blank, 0, D64_SIZE);
    File *f = NULL;
    REQUIRE(fm->fopen(image, FA_WRITE | FA_CREATE_NEW, &f) == FR_OK);
    uint32_t transferred = 0;
    REQUIRE(f->write(blank, D64_SIZE, &transferred) == FR_OK);
    REQUIRE(transferred == D64_SIZE);
    fm->fclose(f);
    delete[] blank;

    File *g = NULL;
    REQUIRE(fm->fopen(image, FA_READ | FA_WRITE, &g) == FR_OK);
    FileSystemInFile_D64 emb(0, true);
    emb.init(g);
    REQUIRE(emb.getFileSystem() != NULL);
    REQUIRE(emb.getFileSystem()->format("BLANK") == FR_OK);
    REQUIRE(emb.getFileSystem()->sync() == FR_OK);
    fm->fclose(g);

    // The listing of the new image must not show the old image's file.
    Path *p = fm->get_new_path("mountcache");
    p->cd(image);
    IndexedList<FileInfo *> listing(16, NULL);
    REQUIRE(fm->get_directory(p, listing, NULL) == FR_OK);
    int old_entries = 0;
    for (int i = 0; i < listing.get_elements(); i++) {
        if (strncmp(listing[i]->lfname, "OLDFILE", 7) == 0) {
            old_entries++;
        }
        delete listing[i];
    }
    listing.clear_list();
    fm->release_path(p);

    printf("%s: entries of the deleted image still listed: %d\n", testname, old_entries);
    REQUIRE(old_entries == 0);
    printf("[%s] passed\n", testname);
}

class ShortTransferFile : public File
{
public:
    uint32_t transferred;
    ShortTransferFile() : File(NULL), transferred(0) { }
    uint32_t get_size(void) { return 1024; }
    FRESULT seek(uint32_t offset) { return FR_OK; }
    FRESULT read(void *buffer, uint32_t len, uint32_t *count) { *count = transferred; return FR_OK; }
    FRESULT write(const void *buffer, uint32_t len, uint32_t *count) { *count = transferred; return FR_OK; }
};

static void short_transfers(void)
{
    const char *testname = "Suite12-ShortTransfers";
    ShortTransferFile file;
    BlockDevice_File block(&file, 512);
    uint8_t buffer[512] = {0};
    REQUIRE(block.read(buffer, 0, 1) == RES_ERROR);
    REQUIRE(block.write(buffer, 0, 1) == RES_ERROR);
    file.transferred = 511;
    REQUIRE(block.read(buffer, 0, 1) == RES_ERROR);
    REQUIRE(block.write(buffer, 0, 1) == RES_ERROR);
    file.transferred = 512;
    REQUIRE(block.read(buffer, 0, 1) == RES_OK);
    REQUIRE(block.write(buffer, 0, 1) == RES_OK);
    printf("[%s] passed\n", testname);
}

static void iso_record(uint8_t *entry, int sector, int size, int flags, const char *name, int len)
{
    entry[0] = (34 + len) & ~1;
    for (int i = 0; i < 4; i++) {
        entry[2 + i] = sector >> (8 * i);
        entry[9 - i] = sector >> (8 * i);
        entry[10 + i] = size >> (8 * i);
        entry[17 - i] = size >> (8 * i);
    }
    entry[25] = flags;
    entry[28] = entry[31] = 1;
    entry[32] = len;
    memcpy(entry + 33, name, len);
}

static void valid_iso(FileManager *fm)
{
    const char *testname = "Suite12-ValidISO";
    uint8_t *image = new uint8_t[20 * 2048]();
    uint8_t *volume = image + 16 * 2048;
    const uint8_t key[] = {1, 'C', 'D', '0', '0', '1', 1, 0};
    memcpy(volume, key, sizeof(key));
    volume[80] = volume[87] = 20;
    volume[129] = volume[130] = 8;
    iso_record(volume + 156, 18, 2048, 2, "", 1);
    image[17 * 2048] = 0xff;
    uint8_t *directory = image + 18 * 2048;
    iso_record(directory, 18, 2048, 2, "", 1);
    iso_record(directory + 34, 18, 2048, 2, "\1", 1);
    iso_record(directory + 68, 19, 4, 0, "KEEP.PRG;1", 10);
    memcpy(image + 19 * 2048, "KEEP", 4);
    FILE *f = ::fopen("output/valid.iso", "wb");
    REQUIRE(f != NULL);
    REQUIRE(::fwrite(image, 2048, 20, f) == 20);
    ::fclose(f);
    delete[] image;
    REQUIRE(copy_to("output/valid.iso", "/Fat/valid.iso") == FR_OK);
    File *inner = NULL;
    REQUIRE(fm->fopen("/Fat/valid.iso/KEEP.PRG", FA_READ, &inner) == FR_OK);
    uint8_t data[4];
    uint32_t transferred;
    REQUIRE(inner->read(data, sizeof(data), &transferred) == FR_OK);
    REQUIRE(transferred == sizeof(data));
    REQUIRE(memcmp(data, "KEEP", sizeof(data)) == 0);
    fm->fclose(inner);
    printf("[%s] passed\n", testname);
}

static void malformed_images(FileManager *fm)
{
    const char *testname = "Suite12-MalformedImages";
    create_file("output/bad.fat", 512);
    FILE *f = ::fopen("output/bad.fat", "r+b");
    REQUIRE(f != NULL);
    REQUIRE(::fseek(f, 11, SEEK_SET) == 0);
    const uint8_t sector_size[] = {0, 2};
    REQUIRE(::fwrite(sector_size, 1, 2, f) == 2);
    ::fclose(f);
    REQUIRE(copy_to("output/bad.fat", "/Fat/bad.fat") == FR_OK);
    Directory *directory = NULL;
    REQUIRE(fm->open_directory("/Fat/bad.fat", &directory) == FR_NO_FILESYSTEM);
    REQUIRE(directory == NULL);
    REQUIRE(copy_to("output/bad.fat", "/Fat/bad.iso") == FR_OK);
    REQUIRE(fm->open_directory("/Fat/bad.iso", &directory) == FR_NO_FILESYSTEM);
    REQUIRE(directory == NULL);
    const uint8_t truncated[12] = {0};
    f = ::fopen("output/short.fat", "wb");
    REQUIRE(f != NULL);
    REQUIRE(::fwrite(truncated, 1, sizeof(truncated), f) == sizeof(truncated));
    ::fclose(f);
    REQUIRE(copy_to("output/short.fat", "/Fat/short.fat") == FR_OK);
    REQUIRE(fm->open_directory("/Fat/short.fat", &directory) == FR_NO_FILESYSTEM);
    REQUIRE(directory == NULL);
    printf("[%s] passed\n", testname);
}

static void create_fat_fixture(const char *testname, const char *path, int blocks)
{
    create_file(path, blocks);
    {
        BlockDevice_Emulated blk(path, 512);
        Partition prt(&blk, 0, 0, 0);
        FileSystemFAT fs(&prt);
        REQUIRE(fs.format("OWNER") == FR_OK);
    }
}

static void fat_image_ownership(FileManager *fm)
{
    const char *testname = "Suite12-FatOwnership";
    create_fat_fixture(testname, "output/owner.fat", 8192);
    REQUIRE(copy_to("output/owner.fat", "/Fat/owner.fat") == FR_OK);
    const uint8_t payload[] = {1, 8, 42};
    write_through_mount(testname, fm, "/Fat/owner.fat/KEEP.prg", payload, sizeof(payload));
    File *writer = NULL, *inner = NULL;
    REQUIRE(fm->fopen("/Fat/owner.fat", FA_READ | FA_WRITE, &writer) == FR_OK);
    uint8_t *before = new uint8_t[2 * 1024 * 1024];
    uint8_t *after = new uint8_t[2 * 1024 * 1024];
    read_image(testname, fm, "/Fat/owner.fat", before, 2 * 1024 * 1024);
    REQUIRE(fm->fopen("/Fat/owner.fat/NEW.prg", FA_WRITE | FA_CREATE_NEW, &inner) == FR_WRITE_PROTECTED);
    REQUIRE(inner == NULL);
    REQUIRE(fm->rename("/Fat/owner.fat/KEEP.prg", "/Fat/owner.fat/NO.prg") == FR_WRITE_PROTECTED);
    REQUIRE(fm->delete_file("/Fat/owner.fat/KEEP.prg") == FR_WRITE_PROTECTED);
    BlockDevice_File protected_device(writer, 512, false);
    REQUIRE(protected_device.status() == STA_PROTECT);
    REQUIRE(protected_device.write(before, 0, 1) == RES_WRPRT);
    read_image(testname, fm, "/Fat/owner.fat", after, 2 * 1024 * 1024);
    REQUIRE(memcmp(before, after, 2 * 1024 * 1024) == 0);
    delete[] before;
    delete[] after;
    fm->fclose(writer);
    REQUIRE(fm->fopen("/Fat/owner.fat/KEEP.prg", FA_READ, &inner) == FR_OK);
    fm->fclose(inner);
    REQUIRE(fm->rename("/Fat/owner.fat/KEEP.prg", "/Fat/owner.fat/RENAMED.prg") == FR_OK);
    REQUIRE(fm->fopen("/Fat/owner.fat", FA_READ | FA_WRITE, &writer) == FR_OK);
    fm->fclose(writer);
    REQUIRE(fm->set_attributes("/Fat/owner.fat", AM_RDO, AM_RDO) == FR_OK);
    REQUIRE(fm->fopen("/Fat/owner.fat/RENAMED.prg", FA_READ, &inner) == FR_OK);
    fm->fclose(inner);
    REQUIRE(fm->fopen("/Fat/owner.fat/RENAMED.prg", FA_WRITE, &inner) == FR_WRITE_PROTECTED);
    REQUIRE(inner == NULL);
    REQUIRE(fm->set_attributes("/Fat/owner.fat", 0, AM_RDO) == FR_OK);
    REQUIRE(fm->fopen("/Fat/owner.fat", FA_READ | FA_WRITE, &writer) == FR_OK);
    fm->fclose(writer);
    printf("[%s] passed\n", testname);
}

static void nested_mount(FileManager *fm)
{
    const char *testname = "Suite12-NestedMount";
    create_iec_d64_fixture("output/nested.d64");
    create_fat_fixture(testname, "output/leaf.fat", 4096);
    REQUIRE(copy_to("output/nested.d64", "/Fat/owner.fat/inner.d64") == FR_OK);
    REQUIRE(copy_to("output/leaf.fat", "/Fat/owner.fat/deep.fat") == FR_OK);
    REQUIRE(copy_to("output/nested.d64", "/Fat/owner.fat/deep.fat/inner.d64") == FR_OK);
    list_image(testname, fm, "/Fat/owner.fat/inner.d64");
    const char *nested = "/Fat/owner.fat/deep.fat/inner.d64";
    Directory *directory = NULL;
    REQUIRE(fm->open_directory(nested, &directory) == FR_OK);
    File *writer = NULL;
    REQUIRE(fm->fopen("/Fat/owner.fat", FA_WRITE | FA_CREATE_ALWAYS, &writer) == FR_LOCKED);
    REQUIRE(writer == NULL);
    FileInfo entry(64);
    REQUIRE(directory->get_entry(entry) == FR_OK);
    delete directory;
    REQUIRE(fm->fopen("/Fat/owner.fat", FA_READ | FA_WRITE, &writer) == FR_OK);
    list_image(testname, fm, "/Fat/owner.fat/inner.d64");
    REQUIRE(fm->open_directory(nested, &directory) == FR_OK);
    fm->fclose(writer);
    delete directory;
    const uint8_t payload[] = {1, 8, 55};
    write_through_mount(testname, fm, "/Fat/owner.fat/deep.fat/inner.d64/NEW.prg", payload, sizeof(payload));
    printf("[%s] passed\n", testname);
}

// Exercise the factory's permissions for every CBM image format, including
// readers, multiple writers, and case-insensitive cache invalidation.
static void write_privileges(FileManager *fm)
{
    const char *testname = "Suite12-WritePrivileges";
    const char *extensions[] = {"d64", "d71", "d81", "dnp"};
    const int sizes[] = {174848, 349696, 819200, 256 * 256};
    const uint8_t payload[] = {1, 8, 17, 23};
    for (int mode = 0; mode < 4; mode++) {
        char image[64], upper[64], inner_path[80], renamed[80];
        sprintf(image, "/Fat/priv.%s", extensions[mode]);
        strcpy(upper, image);
        for (char *p = upper + 5; *p; p++) {
            if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
        }
        sprintf(inner_path, "%s/KEEP.prg", image);
        sprintf(renamed, "%s/RENAMED.prg", image);
        File *file = NULL;
        REQUIRE(fm->fopen(image, FA_WRITE | FA_CREATE_NEW, &file) == FR_OK);
        uint8_t zero[256] = {0};
        uint32_t transferred;
        for (int i = 0; i < sizes[mode] / 256; i++) {
            REQUIRE(file->write(zero, sizeof(zero), &transferred) == FR_OK);
            REQUIRE(transferred == sizeof(zero));
        }
        fm->fclose(file);
        REQUIRE(fm->fopen(image, FA_READ | FA_WRITE, &file) == FR_OK);
        {
            FileSystemInFile_D64 emb(mode, true);
            emb.init(file);
            REQUIRE(emb.getFileSystem()->format("PRIVILEGES") == FR_OK);
            REQUIRE(emb.getFileSystem()->sync() == FR_OK);
        }
        fm->fclose(file);

        // A raw reader does not remove the browser's write privilege.
        File *reader = NULL;
        REQUIRE(fm->fopen(image, FA_READ, &reader) == FR_OK);
        write_through_mount(testname, fm, inner_path, payload, sizeof(payload));
        fm->fclose(reader);

        for (int browser_first = 0; browser_first < 2; browser_first++) {
            if (browser_first) list_image(testname, fm, image);
            File *first = NULL, *second = NULL;
            REQUIRE(fm->fopen(upper, FA_READ | FA_WRITE, &first) == FR_OK);
            list_image(testname, fm, image);
            REQUIRE(fm->fopen(image, FA_READ | FA_WRITE, &second) == FR_OK);
            fm->fclose(first);

            uint8_t *before = new uint8_t[sizes[mode]];
            uint8_t *after = new uint8_t[sizes[mode]];
            REQUIRE(second->seek(0) == FR_OK);
            REQUIRE(second->read(before, sizes[mode], &transferred) == FR_OK);
            REQUIRE(transferred == (uint32_t)sizes[mode]);
            File *inner = NULL;
            REQUIRE(fm->fopen(inner_path, FA_WRITE, &inner) == FR_WRITE_PROTECTED);
            REQUIRE(inner == NULL);
            REQUIRE(fm->fopen(inner_path, FA_WRITE | FA_CREATE_ALWAYS, &inner) == FR_WRITE_PROTECTED);
            REQUIRE(inner == NULL);
            REQUIRE(fm->rename(inner_path, renamed) == FR_WRITE_PROTECTED);
            REQUIRE(fm->delete_file(inner_path) == FR_WRITE_PROTECTED);
            REQUIRE(fm->set_attributes(inner_path, AM_RDO, AM_RDO) == FR_WRITE_PROTECTED);
            REQUIRE(fm->set_dir_label(image, "CHANGED", "XX") == FR_WRITE_PROTECTED);
            REQUIRE(fm->fopen(inner_path, FA_READ, &inner) == FR_OK);
            REQUIRE(inner->write(payload, sizeof(payload), &transferred) == FR_WRITE_PROTECTED);
            REQUIRE(transferred == 0);
            uint8_t data[sizeof(payload)];
            REQUIRE(inner->read(data, sizeof(data), &transferred) == FR_OK);
            REQUIRE(transferred == sizeof(data));
            REQUIRE(memcmp(data, payload, sizeof(data)) == 0);
            fm->fclose(inner);
            REQUIRE(second->seek(0) == FR_OK);
            REQUIRE(second->read(after, sizes[mode], &transferred) == FR_OK);
            REQUIRE(transferred == (uint32_t)sizes[mode]);
            REQUIRE(memcmp(before, after, sizes[mode]) == 0);
            delete[] before;
            delete[] after;
            // Eject while a reader pins the read-only view. The last reader
            // closing must allow a fresh writable view, rather than pin it read-only.
            REQUIRE(fm->fopen(inner_path, FA_READ, &inner) == FR_OK);
            fm->fclose(second);
            fm->fclose(inner);

            REQUIRE(fm->rename(inner_path, renamed) == FR_OK);
            REQUIRE(fm->rename(renamed, inner_path) == FR_OK);
        }
        File *writer = NULL;
        REQUIRE(fm->fopen(image, FA_READ | FA_WRITE, &writer) == FR_OK);
        Directory *directory = NULL;
        REQUIRE(fm->open_directory(image, &directory) == FR_OK);
        fm->fclose(writer);
        delete directory;
        REQUIRE(fm->delete_file(inner_path) == FR_OK);
        REQUIRE(fm->delete_file(image) == FR_OK);
        printf("[%s] %s passed\n", testname, extensions[mode]);
    }
}

// A raw writer must not coexist with a filesystem that cannot be discarded.
static void pinned_mount(FileManager *fm)
{
    const char *testname = "Suite12-PinnedMount";
    create_iec_d64_fixture("output/pinned.d64");
    REQUIRE(copy_to("output/pinned.d64", "/Fat/pinned.d64") == FR_OK);
    const uint8_t payload[] = {1, 8, 42};
    write_through_mount(testname, fm, "/Fat/pinned.d64/KEEP.prg", payload, sizeof(payload));
    File *inner = NULL;
    REQUIRE(fm->fopen("/Fat/pinned.d64/KEEP.prg", FA_READ, &inner) == FR_OK);
    uint8_t *before = new uint8_t[D64_SIZE];
    uint8_t *after = new uint8_t[D64_SIZE];
    read_image(testname, fm, "/Fat/pinned.d64", before);
    File *writer = NULL;
    REQUIRE(fm->fopen("/Fat/PINNED.D64", FA_WRITE | FA_CREATE_ALWAYS, &writer) == FR_LOCKED);
    REQUIRE(writer == NULL);
    const char *aliases[] = {"/Fat/./pinned.d64", "/Fat//pinned.d64", "/Fat/../Fat/pinned.d64"};
    const FRESULT results[] = {FR_LOCKED, FR_NO_PATH, FR_LOCKED}; // Empty path components are invalid.
    for (int i = 0; i < 3; i++) {
        REQUIRE(fm->fopen(aliases[i], FA_WRITE | FA_CREATE_ALWAYS, &writer) == results[i]);
        REQUIRE(writer == NULL);
    }
    read_image(testname, fm, "/Fat/pinned.d64", after);
    REQUIRE(memcmp(before, after, D64_SIZE) == 0);
    delete[] before;
    delete[] after;
    uint8_t data[sizeof(payload)];
    uint32_t transferred = 0;
    REQUIRE(inner->read(data, sizeof(data), &transferred) == FR_OK);
    REQUIRE(transferred == sizeof(data));
    REQUIRE(memcmp(data, payload, sizeof(data)) == 0);
    fm->fclose(inner);
    REQUIRE(fm->fopen("/Fat/pinned.d64", FA_READ | FA_WRITE, &writer) == FR_OK);
    fm->fclose(writer);
    Directory *directory = NULL;
    REQUIRE(fm->open_directory("/Fat/pinned.d64", &directory) == FR_OK);
    REQUIRE(fm->fopen("/Fat/pinned.d64", FA_READ | FA_WRITE, &writer) == FR_LOCKED);
    REQUIRE(writer == NULL);
    delete directory;
    REQUIRE(fm->fopen("/Fat/pinned.d64/KEEP.prg", FA_WRITE, &inner) == FR_OK);
    REQUIRE(fm->fopen("/Fat/pinned.d64", FA_READ | FA_WRITE, &writer) == FR_LOCKED);
    REQUIRE(writer == NULL);
    fm->fclose(inner);
    REQUIRE(fm->fopen("/Fat/pinned.d64", FA_READ | FA_WRITE, &writer) == FR_OK);
    fm->fclose(writer);
    printf("[%s] passed\n", testname);
}

static void short_name_alias(FileManager *fm)
{
    const char *testname = "Suite12-ShortNameAlias";
    create_iec_d64_fixture("output/longname.d64");
    REQUIRE(copy_to("output/longname.d64", "/Fat/longcachedimage.d64") == FR_OK);
    File *image = NULL, *alias = NULL;
    REQUIRE(fm->fopen("/Fat/longcachedimage.d64", FA_READ, &image) == FR_OK);
    REQUIRE(image->get_file_system()->file_open("/LONGCA~1.D64", FA_READ, &alias) == FR_OK);
    REQUIRE(image->get_inode() != 0 && image->get_inode() == alias->get_inode());
    alias->close();
    fm->fclose(image);
    const uint8_t payload[] = {1, 8, 77};
    write_through_mount(testname, fm, "/Fat/longcachedimage.d64/KEEP.prg", payload, sizeof(payload));
    File *reader = NULL, *writer = NULL;
    REQUIRE(fm->fopen("/Fat/longcachedimage.d64/KEEP.prg", FA_READ, &reader) == FR_OK);
    uint8_t *before = new uint8_t[D64_SIZE];
    uint8_t *after = new uint8_t[D64_SIZE];
    read_image(testname, fm, "/Fat/longcachedimage.d64", before);
    const uint8_t modes[] = {FA_CREATE_ALWAYS, FA_OPEN_ALWAYS, FA_CREATE_NEW};
    for (int i = 0; i < 3; i++) {
        REQUIRE(fm->fopen("/Fat/LONGCA~1.D64", FA_WRITE | modes[i], &writer) == FR_EXIST);
        REQUIRE(writer == NULL);
    }
    REQUIRE(fm->fopen("/Fat/LONGCA~1.D64", FA_READ | FA_WRITE, &writer) == FR_NO_FILE);
    REQUIRE(writer == NULL);
    read_image(testname, fm, "/Fat/longcachedimage.d64", after);
    REQUIRE(memcmp(before, after, D64_SIZE) == 0);
    delete[] before;
    delete[] after;
    uint8_t data[sizeof(payload)];
    uint32_t transferred;
    REQUIRE(reader->read(data, sizeof(data), &transferred) == FR_OK);
    REQUIRE(transferred == sizeof(data));
    REQUIRE(memcmp(data, payload, sizeof(data)) == 0);
    fm->fclose(reader);
    printf("[%s] passed\n", testname);
}

void execute_suite12(FileManager *fm)
{
    short_name_alias(fm);
    short_transfers();
    valid_iso(fm);
    malformed_images(fm);
    fat_image_ownership(fm);
    nested_mount(fm);
    write_privileges(fm);
    pinned_mount(fm);
    browser_first(fm);
    drive_holds_image(fm);
    no_eject(fm);
    recreate_same_name(fm);
}
