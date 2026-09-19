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
#include "macros.h"

void create_iec_d64_fixture(const char *path);
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

static void read_image(const char *testname, FileManager *fm, const char *image, uint8_t *buffer)
{
    File *f = NULL;
    REQUIRE(fm->fopen(image, FA_READ, &f) == FR_OK);
    uint32_t transferred = 0;
    REQUIRE(f->read(buffer, D64_SIZE, &transferred) == FR_OK);
    REQUIRE(transferred == D64_SIZE);
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

// The browser enters the image, the drive then mounts it and writes, and the
// browser copies into the image while the drive still holds it: the image is
// never ejected.
static void no_eject(FileManager *fm)
{
    const char *testname = "Suite12-NoEject";
    printf("\n[%s] Browser writes the image while a second handle still holds it\n", testname);

    const char *image = "/Fat/stale3.d64";
    create_iec_d64_fixture("output/stale3.d64");
    REQUIRE(copy_to("output/stale3.d64", image) == FR_OK);

    list_image(testname, fm, image);

    File *drive = NULL;
    REQUIRE(fm->fopen(image, FA_READ | FA_WRITE, &drive) == FR_OK);

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
    write_through_mount(testname, fm, "/Fat/stale3.d64/BROWSERC", other, PAYLOAD_SIZE);

    check_payload_survived(testname, fm, image, payload, "DRIVEC");

    fm->fclose(drive);
    delete[] payload;
    delete[] other;
    printf("[%s] passed\n", testname);
}

void execute_suite12(FileManager *fm)
{
    browser_first(fm);
    drive_holds_image(fm);
    no_eject(fm);
}
