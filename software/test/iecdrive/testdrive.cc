#include "iec_drive.h"
#include "dump_hex.h"
#include "filemanager.h"
#include "file_device.h"
#include "filesystem_fat.h"
#include "macros.h"
#include "current_time.h"
#include <unistd.h>
#include <string.h>

void outbyte(int c) { putc(c, stdout); }
CommandInterface cmd_if;

BlockDevice *ramdisk_blk;
FileDevice *ramdisk_node;

BlockDevice *flashdisk_blk;
FileDevice *flashdisk_node;

char last_status[300]; // a status, or a binary reply of up to 256 bytes
int negative_fetches = 0; // prefetch_more offering fewer than zero bytes, which a real reader copies
int last_status_size;
void create_iec_d64_fixture(const char *path);
void create_iec_geos_fixture(const char *path);
void execute_suite12(FileManager *fm);
void create_iec_d81_fixture(const char *path);
FRESULT copy_to(const char *from, const char *to);
void open_file(IecDrive *dr, uint8_t chan, const char *fn);
void get_status(IecDrive *dr);
void close_file(IecDrive *dr, uint8_t chan);
int read_file(IecDrive *dr, uint8_t chan, uint8_t *out, int len);
static void send_channel_data(IecDrive *dr, uint8_t chan, const uint8_t *data, int len);
static void expect_status_ok(const char *testname, const char *context);
static void expect_command_ok(const char *testname, IecDrive *dr, const char *cmd);
static const char *send_command(IecDrive *dr, const char *cmd);
static void expect_status_prefix(const char *testname, const char *context, const char *prefix);

static void save_fixture_file(FileManager *fm, const char *path, const char *name, const char *payload)
{
    const char *testname = "save_fixture_file";
    uint32_t transferred = 0;
    FRESULT fres = fm->save_file(false, path, name, (const uint8_t *)payload, strlen(payload), &transferred);
    if (fres != FR_OK || transferred != strlen(payload)) {
        printf("%s/%s: save result %d, transferred %u\n", path, name, fres, transferred);
    }
    REQUIRE(fres == FR_OK);
    REQUIRE(transferred == strlen(payload));

    // printf("Write done. Verify:\n");
    File *verify = NULL;
    fres = fm->fopen(path, name, FA_READ, &verify);
    if (fres == FR_OK) {
        fm->fclose(verify);
    } else {
        printf("verify fixture '%s/%s': %s\n", path, name, FileSystem::get_error_string(fres));
    }
    REQUIRE(fres == FR_OK);
}

static void verify_fixture_file(FileManager *fm, const char *path, const char *name, const char *payload)
{
    const char *testname = "verify_fixture_file";
    File *verify = NULL;
    FRESULT fres = fm->fopen(path, name, FA_READ, &verify);
    printf("verify fixture '%s/%s': %s\n", path, name, FileSystem::get_error_string(fres));
    if (fres == FR_OK) {
        fm->fclose(verify);
    }
    REQUIRE(fres == FR_OK);
}

static void create_iec_fat_fixture(FileManager *fm, const char *path)
{
    const char *testname = "create_iec_fat_fixture";
    FRESULT fres = fm->create_dir(path);
    REQUIRE(fres == FR_OK || fres == FR_EXIST);

    save_fixture_file(fm, path, "BASIC.prg", "BASIC:PRG");
    save_fixture_file(fm, path, "BASIC.seq", "BASIC:SEQ");
    save_fixture_file(fm, path, "Q.{C6C3C4C3C6C3C4C3C6C3C4C3C6C3}.prg", "NASTY:PRG");
    save_fixture_file(fm, path, "Q.{C6C3C4C3C6C3C4C3C6C3C4C3C6C4}.prg", "NASTY2:PRG");
    save_fixture_file(fm, path, "LITERAL.prg", "LITERAL:PRG");
    save_fixture_file(fm, path, "LITERAL.PRG{}.prg", "LITERAL.PRG:PRG");
    save_fixture_file(fm, path, "LITERAL.PRG{}.seq", "LITERAL.PRG:SEQ");
    save_fixture_file(fm, path, "ONLYBASE.prg", "ONLYBASE:PRG");
}

static void prepare_disk_partition(const char *image_path, const char *mounted_path,
                                   IecDrive *dr, int part, const char *label)
{
    const char *testname = "prepare_disk_partition";
    REQUIRE(copy_to(image_path, mounted_path) == FR_OK);
    dr->add_partition(part, mounted_path, label);
}

static void prepare_fat_partition(FileManager *fm, IecDrive *dr, const char *path,
                                  int part, const char *label)
{
    const char *testname = "prepare_fat_partition";
    FRESULT fres = fm->create_dir(path);
    REQUIRE(fres == FR_OK || fres == FR_EXIST);
    dr->add_partition(part, path, label);
}

static void print_scenario(const char *suite, const char *scenario)
{
    printf("\n[%s] %s\n", suite, scenario);
}

struct MountedImageCase {
    void (*create_fixture)(const char *path);
    const char *source;
    const char *mount;
    const char *label;
};

struct BlockCase {
    void (*create_fixture)(const char *path);
    const char *source;
    const char *mount;
    const char *label;
    int partition;
    int read_track;
    int read_sector;
    int write_track;
    int write_sector;
    int bam_track;
    int bam_sector;
    bool d81;
};

template <size_t N, typename Fn>
static void run_image_matrix(const char *suite, const char *scenario,
                             const MountedImageCase (&cases)[N], Fn fn)
{
    print_scenario(suite, scenario);
    for (size_t i = 0; i < N; i++) {
        cases[i].create_fixture(cases[i].source);
        fn(cases[i]);
    }
}

static int d64_abs_sector(int track, int sector)
{
    const char *testname = "d64_abs_sector";
    static const int sectors_per_track[] = {
        0,
        21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
        19, 19, 19, 19, 19, 19, 19,
        18, 18, 18, 18, 18, 18,
        17, 17, 17, 17, 17
    };
    REQUIRE(track >= 1);
    REQUIRE(track <= 35);
    REQUIRE(sector >= 0);
    REQUIRE(sector < sectors_per_track[track]);

    int abs = sector;
    for (int t = 1; t < track; t++) {
        abs += sectors_per_track[t];
    }
    return abs;
}

static int d81_abs_sector(int track, int sector)
{
    const char *testname = "d81_abs_sector";
    REQUIRE(track >= 1);
    REQUIRE(track <= 80);
    REQUIRE(sector >= 0);
    REQUIRE(sector < 40);
    return (track - 1) * 40 + sector;
}

static void make_sector_payload(uint8_t *buffer, const char *payload)
{
    const char *testname = "make_sector_payload";
    memset(buffer, 0, 256);
    size_t len = strlen(payload);
    REQUIRE(len <= 254);
    buffer[0] = 0;
    buffer[1] = (uint8_t)(len + 1);
    memcpy(buffer + 2, payload, len);
}

static void make_increment_pattern(uint8_t *buffer)
{
    for (int i = 0; i < 256; i++) {
        buffer[i] = (uint8_t)i;
    }
}

static uint32_t get_free_sectors(FileManager *fm, const char *path)
{
    const char *testname = "get_free_sectors";
    Path p(path);
    uint32_t free = 0;
    uint32_t cluster_size = 0;
    FRESULT fres = fm->get_free(&p, free, cluster_size);
    REQUIRE(fres == FR_OK);
    REQUIRE(cluster_size == 256);
    return free;
}

static void expect_command_status_prefix(const char *testname, IecDrive *dr,
                                         const char *cmd, const char *prefix)
{
    const char *status = send_command(dr, cmd);
    (void)status;
    expect_status_prefix(testname, cmd, prefix);
}

static void open_buffer_channel(const char *testname, IecDrive *dr, uint8_t chan)
{
    const char *name = "#";
    open_file(dr, chan, name);
    get_status(dr);
    expect_status_ok(testname, name);
}

static void read_buffer_channel(const char *testname, IecDrive *dr, uint8_t chan,
                                uint8_t *buffer, int len);

// U1 and U2 move whole blocks; B-R and B-W use the first byte as a length (SI-094). The
// partition number is ignored: the channel uses the partition current at its open (SI-093).
static void capture_block_sector(const char *testname, IecDrive *dr, uint8_t chan,
                                 int part, int track, int sector, uint8_t *out)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "U1 %u %d %d %d", chan, part, track, sector);
    expect_command_ok(testname, dr, cmd);
    read_buffer_channel(testname, dr, chan, out, 256);
}

static void read_buffer_channel(const char *testname, IecDrive *dr, uint8_t chan,
                                uint8_t *buffer, int len)
{
    memset(buffer, 0, len);
    int got = read_file(dr, chan, buffer, len);
    if (got != len) {
        printf("%s: buffer channel %u read %d bytes, expected %d\n", testname, chan, got, len);
        dump_hex_relative(buffer, got);
    }
    REQUIRE(got == len);
}

static void expect_buffer_sector(const char *testname, IecDrive *dr, uint8_t chan,
                                 int part, int track, int sector, const uint8_t *expected)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "U1 %u %d %d %d", chan, part, track, sector);
    expect_command_ok(testname, dr, cmd);
    uint8_t actual[256];
    read_buffer_channel(testname, dr, chan, actual, sizeof(actual));
    if (memcmp(actual, expected, sizeof(actual)) != 0) {
        printf("%s: block read mismatch for %d/%d\n", testname, track, sector);
        dump_hex_relative(actual, sizeof(actual));
    }
    REQUIRE(memcmp(actual, expected, sizeof(actual)) == 0);
}

static void expect_block_write_sector(const char *testname, IecDrive *dr, uint8_t chan,
                                      int part, int track, int sector, const uint8_t *payload)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "U2 %u %d %d %d", chan, part, track, sector);
    expect_command_ok(testname, dr, cmd);
    uint8_t actual[256];
    snprintf(cmd, sizeof(cmd), "U1 %u %d %d %d", chan, part, track, sector);
    expect_command_ok(testname, dr, cmd);
    read_buffer_channel(testname, dr, chan, actual, sizeof(actual));
    if (memcmp(actual, payload, sizeof(actual)) != 0) {
        printf("%s: block write/readback mismatch for %d/%d\n", testname, track, sector);
        dump_hex_relative(actual, sizeof(actual));
    }
    REQUIRE(memcmp(actual, payload, sizeof(actual)) == 0);
}

static void expect_bam_sector_state(const char *testname, IecDrive *dr, uint8_t chan,
                                    const BlockCase &c, const uint8_t *before,
                                    bool expect_allocated, int expected_free_delta)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "U1 %u %d %d %d", chan, c.partition, c.bam_track, c.bam_sector);
    expect_command_ok(testname, dr, cmd);
    uint8_t now[256];
    read_buffer_channel(testname, dr, chan, now, sizeof(now));

    int entry_offset = c.d81 ? (16 + 6 * ((c.write_track - 1) % 40)) : (4 * c.write_track);
    if (now[entry_offset] != (uint8_t)(before[entry_offset] + expected_free_delta)) {
        printf("%s: free count mismatch at track %d: got %u, expected %u\n",
               testname, c.write_track, now[entry_offset], (uint8_t)(before[entry_offset] + expected_free_delta));
    }
    REQUIRE(now[entry_offset] == (uint8_t)(before[entry_offset] + expected_free_delta));

    uint8_t bit = (uint8_t)(1 << (c.write_sector & 7));
    uint8_t before_byte = before[entry_offset + 1 + (c.write_sector >> 3)];
    uint8_t now_byte = now[entry_offset + 1 + (c.write_sector >> 3)];
    if (expect_allocated) {
        REQUIRE((before_byte & bit) != 0);
        REQUIRE((now_byte & bit) == 0);
    } else {
        REQUIRE((before_byte & bit) == 0);
        REQUIRE((now_byte & bit) != 0);
    }
}

void show_partitions(UserInterface *ui, IecFileSystem *fs)
{

}

void form_new_partition(UserInterface*, JSON_Object*)
{
}

void init_ram_disk()
{
    const int sz = 512;
    const int size = 4 * 1024 * 1024;
    const int sectors = size / sz;
    uint8_t *ramdisk_mem = new uint8_t[size]; // 4 MB only
    ramdisk_blk = new BlockDevice_Ram(ramdisk_mem, sz, sectors);
    {
        FileSystem *ramdisk_fs;
        Partition *ramdisk_prt;
        ramdisk_prt = new Partition(ramdisk_blk, 0, 0, 0);
        ramdisk_fs  = new FileSystemFAT(ramdisk_prt);
        ramdisk_fs->format("RamDisk");
        delete ramdisk_fs;
        delete ramdisk_prt;
    }
    ramdisk_node = new FileDevice(ramdisk_blk, "Temp", "RAM Disk");
    ramdisk_node->attach_disk(sz);
    FileManager :: getFileManager()->add_root_entry(ramdisk_node);
}

void init_flash_disk()
{
    const int sz = 512;
    const int size = 1024 * 1024;
    const int sectors = size / sz;
    uint8_t *flashdisk_mem = new uint8_t[size]; // 1 MB only
    flashdisk_blk = new BlockDevice_Ram(flashdisk_mem, sz, sectors);
    {
        FileSystem *flashdisk_fs;
        Partition *flashdisk_prt;
        flashdisk_prt = new Partition(flashdisk_blk, 0, 0, 0);
        flashdisk_fs  = new FileSystemFAT(flashdisk_prt);
        flashdisk_fs->format("FlashDisk");
        delete flashdisk_fs;
        delete flashdisk_prt;
    }
    flashdisk_node = new FileDevice(flashdisk_blk, "Flash", "Internal Flash");
    flashdisk_node->attach_disk(sz);
    FileManager :: getFileManager()->add_root_entry(flashdisk_node);
}

void create_file(const char *filename, int blocks)
{
    uint8_t block[256];
    memset(block, 0, 256);
    FILE *f = fopen(filename, "wb");
    for(int i=0;i<blocks;i++) {
        fwrite(block, 256, 1, f);
    }
    fclose(f);
}

#include "blockdev_emul.h"
#include "stream_textlog.h"
#include <malloc.h>

// The FAT file, which also tells Suite11-CR6-Lock whether a watched drive held its lock
// when one of its entry points reached storage.
extern IecDrive *s11_lock_watch;
extern int s11_lock_watch_locked;
extern int s11_lock_watch_unlocked;
extern int iec_drive_lock_depth(IecDrive *drive) __attribute__((weak));
class LockWatchingBlockDevice : public BlockDevice_Emulated
{
    void note(void)
    {
        if (!s11_lock_watch) {
            return;
        }
        if (iec_drive_lock_depth && iec_drive_lock_depth(s11_lock_watch)) {
            s11_lock_watch_locked++;
        } else {
            s11_lock_watch_unlocked++;
        }
    }
public:
    LockWatchingBlockDevice(const char *name, int sec_size) : BlockDevice_Emulated(name, sec_size) { }
    DRESULT read(uint8_t *buf, uint32_t sector, int count) { note(); return BlockDevice_Emulated::read(buf, sector, count); }
    DRESULT write(const uint8_t *buf, uint32_t sector, int count) { note(); return BlockDevice_Emulated::write(buf, sector, count); }
};

void init_fat_file()
{
    create_file("format.fat", 64*1024); // 16 MB: room for the images Suite10 and Suite11 create
    static LockWatchingBlockDevice blk("format.fat", 512);
    static Partition prt(&blk, 0, 0, 0);
    static FileSystemFAT fs(&prt);
    fs.format("GIDEON");
    if (!fs.init()) {
        printf("Initialization of FAT file system failed.\n");
        return;
    }
    static FileDevice *fatdisk_node = new FileDevice(&blk, "Fat", "FAT File On Disk");
    fatdisk_node->attach_disk(512);
    FileManager :: getFileManager()->add_root_entry(fatdisk_node);
}


FRESULT copy_from(const char *from, const char *to)
{
    printf("Copying %s to %s.\n", from, to);
    File *fi = NULL;
    FileManager *fm = FileManager :: getFileManager();
    FRESULT fres = fm->fopen(from, FA_READ, &fi);
    uint8_t buffer[1024];
    if (fres == FR_OK) {
        FILE *fo = fopen(to, "wb");
        uint32_t tr;
        do {
            fres = fi->read(buffer, 1024, &tr);
            //printf("%d\n", tr);
            fwrite(buffer, 1, tr, fo);
        } while(tr);
        fclose(fo);
        fm->fclose(fi);
    } else {
        printf("File '%s' not found. (%s)\n", from, FileSystem::get_error_string(fres));
    }
    return fres;
}

FRESULT copy_to(const char *from, const char *to)
{
    printf("Copying %s to %s.\n", from, to);
    File *fo;
    FileManager *fm = FileManager :: getFileManager();
    uint8_t buffer[1024];

    FILE *fi = fopen(from, "rb");
    if (!fi) {
        return FR_NO_FILE;
    }

    FRESULT fres = fm->fopen(to, FA_WRITE | FA_CREATE_ALWAYS, &fo);
    if (fres == FR_OK) {
        uint32_t tr;
        do {
            tr = fread(buffer, 1, 1024, fi);
            fres = fo->write(buffer, tr, &tr);
        } while(tr);
        fclose(fi);
        fm->fclose(fo);
    } else {
        printf("File '%s' could not be opened for writing. (%s)\n", to, FileSystem::get_error_string(fres));
    }
    return fres;
}

void get_status(IecDrive *dr)
{
    uint8_t *data;
    int data_size;

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x6f); // channel 15
    dr->talk();
    dr->prefetch_more(256, data, data_size);

    memcpy(last_status, data, data_size);
    last_status_size = data_size;
    last_status[data_size] = 0;
    data[data_size] = 0;
    //printf("Status: %s\n", data);
    //dump_hex_relative(data, data_size);
    dr->pop_more(data_size);
}

const char *send_command(IecDrive *dr, const char *cmd)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x6F); // open channel 15
    for(int i=0;i<strlen(cmd);i++) {
        dr->push_data((uint8_t)cmd[i]);
    }
    dr->push_ctrl(SLAVE_CMD_EOI);
    get_status(dr);
    return last_status;
}

void open_file(IecDrive *dr, uint8_t chan, const char *fn)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xF0 | chan);
    while(*fn) dr->push_data(*(fn++));
    dr->push_ctrl(SLAVE_CMD_EOI);
}

void open_file(IecDrive *dr, const char *fn)
{
    open_file(dr, 0, fn);
}

int read_file(IecDrive *dr, uint8_t chan, uint8_t *out, int len)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x60 | chan);
    dr->talk();
    int ret;
    uint8_t *data;
    int data_size;
    int total_read = 0;
    while(len > 0) {
        ret = dr->prefetch_more(256, data, data_size);
        // printf("Ret: %d Count = %d\n", ret, data_size);
        if (data_size < 0) {
            printf("TESTDRIVE: channel %u prefetch_more offered %d bytes\n", chan, data_size);
            negative_fetches++;
            break;
        }
        if (data_size == 0) {
            break;
        }
        int reading = (data_size > len) ? len : data_size;
        if(out) {
            memcpy(out, data, reading);
            out += reading;
        }
        len -= reading;
        total_read += reading;
        dr->pop_more(reading);
        if (ret != 0) {
            break;
        }
    }
    return total_read;
}

int read_file(IecDrive *dr, uint8_t *out, int len)
{
    return read_file(dr, 0, out, len);
}

int read_file_limited(IecDrive *dr, uint8_t chan, uint8_t *out, int len)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x60 | chan);
    dr->talk();
    int ret;
    uint8_t *data;
    int data_size;
    int total_read = 0;
    while(len > 0) {
        ret = dr->prefetch_more(len, data, data_size);
        if (data_size < 0) {
            printf("TESTDRIVE: channel %u prefetch_more offered %d bytes\n", chan, data_size);
            negative_fetches++;
            break;
        }
        if (data_size == 0) {
            break;
        }
        if(out) {
            memcpy(out, data, data_size);
            out += data_size;
        }
        len -= data_size;
        total_read += data_size;
        dr->pop_more(data_size);
        if (ret != 0) {
            break;
        }
    }
    return total_read;
}

void write_file(IecDrive *dr, uint8_t chan, const char *fn, const char *msg)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xF0 | chan);
    while(*fn) dr->push_data(*(fn++));
    dr->push_ctrl(SLAVE_CMD_EOI);

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x60 | chan);
    while(*msg) dr->push_data(*(msg++));
    dr->push_ctrl(SLAVE_CMD_EOI);

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xE0 | chan);
    dr->push_ctrl(SLAVE_CMD_EOI);
}

void close_file(IecDrive *dr, uint8_t chan)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xE0 | chan);

    get_status(dr);
}

void close_read_file(IecDrive *dr)
{
    close_file(dr, 0);
}

static void expect_status_ok(const char *testname, const char *context)
{
    if (strcmp(last_status, "00, OK,00,00\r") != 0) {
        printf("%s: %s: status was '%s'\n", testname, context, last_status);
    }
    REQUIRE(strcmp(last_status, "00, OK,00,00\r") == 0);
}

static void expect_file_not_found(const char *testname, const char *context)
{
    if (strncmp(last_status, "62,FILE NOT FOUND", 17) != 0) {
        printf("%s: %s: status was '%s'\n", testname, context, last_status);
    }
    REQUIRE(strncmp(last_status, "62,FILE NOT FOUND", 17) == 0);
}

static void expect_iec_file(const char *testname, IecDrive *dr, uint8_t chan, const char *name, const char *expected)
{
    uint8_t buffer[256];
    memset(buffer, 0, sizeof(buffer));

    open_file(dr, chan, name);
    get_status(dr);
    expect_status_ok(testname, name);

    int got = read_file(dr, chan, buffer, sizeof(buffer));
    int expected_len = strlen(expected);
    if (got != expected_len || memcmp(buffer, expected, expected_len) != 0) {
        printf("%s: %s on channel %u: got %d bytes, expected %d bytes '%s'\n",
                testname, name, chan, got, expected_len, expected);
        dump_hex_relative(buffer, got);
    }
    REQUIRE(got == expected_len);
    REQUIRE(memcmp(buffer, expected, expected_len) == 0);

    close_file(dr, chan);
    expect_status_ok(testname, name);
}

static void expect_iec_file_missing(const char *testname, IecDrive *dr, uint8_t chan, const char *name)
{
    open_file(dr, chan, name);
    get_status(dr);
    expect_file_not_found(testname, name);
    close_file(dr, chan);
}

static void expect_command_response(const char *testname, IecDrive *dr, const char *cmd, const char *expected)
{
    const char *status = send_command(dr, cmd);
    if (strcmp(status, expected) != 0) {
        char copy[64];
        strncpy(copy, status, 63);
        int len = strlen(copy);
        if ((len > 0) && (copy[len-1] == 0x0d)) { // cut off the trailing newline
            copy[len-1] = 0;
        }
        printf("%s: %s: response was '%s', expected '%s'\n", testname, cmd, copy, expected);
    }
    REQUIRE(strcmp(status, expected) == 0);
}

static void expect_command_ok(const char *testname, IecDrive *dr, const char *cmd)
{
    expect_command_response(testname, dr, cmd, "00, OK,00,00\r");
}

// The same, for a command whose parameters are binary and so cannot be a C string.
static void send_command_data(IecDrive *dr, const uint8_t *data, int len);

static void expect_command_data_response(const char *testname, IecDrive *dr,
                                         const uint8_t *cmd, int len, const char *expected)
{
    send_command_data(dr, cmd, len);
    get_status(dr);
    if (strcmp(last_status, expected) != 0) {
        printf("%s: response was '%s', expected '%s'\n", testname, last_status, expected);
        dump_hex_relative(cmd, len);
    }
    REQUIRE(strcmp(last_status, expected) == 0);
}

static void expect_command_bytes(const char *testname, IecDrive *dr, const char *cmd,
                                 const uint8_t *expected, int expected_len)
{
    send_command(dr, cmd);
    if (last_status_size != expected_len || memcmp(last_status, expected, expected_len) != 0) {
        printf("%s: %s: got %d response bytes, expected %d\n",
               testname, cmd, last_status_size, expected_len);
        dump_hex_relative((uint8_t *)last_status, last_status_size);
    }
    REQUIRE(last_status_size == expected_len);
    REQUIRE(memcmp(last_status, expected, expected_len) == 0);
}

static void expect_fresult(const char *testname, const char *context, FRESULT got, FRESULT expected)
{
    if (got != expected) {
        printf("%s: %s: got %s, expected %s\n", testname, context,
               FileSystem::get_error_string(got), FileSystem::get_error_string(expected));
    }
    REQUIRE(got == expected);
}

static void expect_transferred(const char *testname, const char *context, uint32_t got, uint32_t expected)
{
    if (got != expected) {
        printf("%s: %s: transferred %u bytes, expected %u\n", testname, context, got, expected);
    }
    REQUIRE(got == expected);
}

static int expect_directory_read(const char *testname, IecDrive *dr, const char *name)
{
    uint8_t buffer[8192];
    memset(buffer, 0, sizeof(buffer));

    open_file(dr, 0, name);
    get_status(dr);
    expect_status_ok(testname, name);

    int got = read_file(dr, 0, buffer, sizeof(buffer));
    if (got <= 0) {
        printf("%s: %s: directory read returned %d bytes\n", testname, name, got);
    }
    REQUIRE(got > 0);

    close_file(dr, 0);
    expect_status_ok(testname, name);
    return got;
}

static void expect_directory_contains(const char *testname, IecDrive *dr,
                                      const char *name, const char *expected)
{
    uint8_t buffer[8192];
    memset(buffer, 0, sizeof(buffer));

    open_file(dr, 0, name);
    get_status(dr);
    expect_status_ok(testname, name);

    int got = read_file(dr, 0, buffer, sizeof(buffer));
    if (got <= 0) {
        printf("%s: %s: directory read returned %d bytes\n", testname, name, got);
    }
    REQUIRE(got > 0);

    int expected_len = strlen(expected);
    bool found = false;
    for (int i = 0; i <= got - expected_len; i++) {
        if (memcmp(buffer + i, expected, expected_len) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        printf("%s: %s: directory listing did not contain '%s'\n",
               testname, name, expected);
        dump_hex_relative(buffer, got);
    }
    REQUIRE(found);

    close_file(dr, 0);
    expect_status_ok(testname, name);
}

static void send_channel_data(IecDrive *dr, uint8_t chan, const uint8_t *data, int len)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x60 | chan);
    for (int i = 0; i < len; i++) {
        dr->push_data(data[i]);
    }
    dr->push_ctrl(SLAVE_CMD_EOI);
}

static void expect_iec_write_ok(const char *testname, IecDrive *dr, uint8_t chan,
                                const char *name, const char *payload)
{
    open_file(dr, chan, name);
    get_status(dr);
    expect_status_ok(testname, name);

    send_channel_data(dr, chan, (const uint8_t *)payload, strlen(payload));
    get_status(dr);
    expect_status_ok(testname, "IEC write");

    close_file(dr, chan);
    expect_status_ok(testname, name);
}

static void expect_iec_open_status_prefix(const char *testname, IecDrive *dr, uint8_t chan,
                                          const char *name, const char *prefix)
{
    open_file(dr, chan, name);
    get_status(dr);
    int len = strlen(prefix);
    if (strncmp(last_status, prefix, len) != 0) {
        printf("%s: %s: status was '%s', expected prefix '%s'\n",
               testname, name, last_status, prefix);
    }
    REQUIRE(strncmp(last_status, prefix, len) == 0);
}

static void send_command_data(IecDrive *dr, const uint8_t *data, int len);

static void expect_iec_seek_read(const char *testname, IecDrive *dr, uint8_t chan,
                                 const char *name, uint32_t pos, const char *expected)
{
    uint8_t buffer[256];
    memset(buffer, 0, sizeof(buffer));

    open_file(dr, chan, name);
    get_status(dr);
    expect_status_ok(testname, name);

    uint8_t cmd[6] = {
        'P',
        chan,
        (uint8_t)(pos & 0xFF),
        (uint8_t)((pos >> 8) & 0xFF),
        (uint8_t)((pos >> 16) & 0xFF),
        (uint8_t)((pos >> 24) & 0xFF)
    };
    send_command_data(dr, cmd, sizeof(cmd));
    get_status(dr);
    expect_status_ok(testname, "file position");

    int got = read_file(dr, chan, buffer, sizeof(buffer));
    int expected_len = strlen(expected);
    if (got != expected_len || memcmp(buffer, expected, expected_len) != 0) {
        printf("%s: %s after pos %u on channel %u: got %d bytes, expected %d bytes '%s'\n",
                testname, name, pos, chan, got, expected_len, expected);
        dump_hex_relative(buffer, got);
    }
    REQUIRE(got == expected_len);
    REQUIRE(memcmp(buffer, expected, expected_len) == 0);

    close_file(dr, chan);
    expect_status_ok(testname, name);
}

static void send_command_data(IecDrive *dr, const uint8_t *data, int len)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x6F);
    for (int i = 0; i < len; i++) {
        dr->push_data(data[i]);
    }
    dr->push_ctrl(SLAVE_CMD_EOI);
}

static void expect_status_prefix(const char *testname, const char *context, const char *prefix)
{
    int len = strlen(prefix);
    if (strncmp(last_status, prefix, len) != 0) {
        printf("%s: %s: status was '%s', expected prefix '%s'\n",
               testname, context, last_status, prefix);
    }
    REQUIRE(strncmp(last_status, prefix, len) == 0);
}

static void expect_current_status(const char *testname, const char *context, const char *expected)
{
    if (strcmp(last_status, expected) != 0) {
        printf("%s: %s: status was '%s', expected '%s'\n",
               testname, context, last_status, expected);
    }
    REQUIRE(strcmp(last_status, expected) == 0);
}

static void expect_rel_open(const char *testname, IecDrive *dr, uint8_t chan,
                            const char *name, uint8_t record_size)
{
    char filename[80];
    int len = snprintf(filename, sizeof(filename), "%s,L,", name);
    REQUIRE(len > 0);
    REQUIRE(len + 2 < (int)sizeof(filename));
    filename[len++] = (char)record_size;
    filename[len] = 0;

    open_file(dr, chan, filename);
    get_status(dr);
    expect_status_ok(testname, name);
}

static void expect_rel_open_status_prefix(const char *testname, IecDrive *dr, uint8_t chan,
                                          const char *name, uint8_t record_size,
                                          const char *prefix)
{
    char filename[80];
    int len = snprintf(filename, sizeof(filename), "%s,L", name);
    REQUIRE(len > 0);
    REQUIRE(len + 3 < (int)sizeof(filename));
    if (record_size) {
        filename[len++] = ',';
        filename[len++] = (char)record_size;
        filename[len] = 0;
    }
    open_file(dr, chan, filename);
    get_status(dr);
    expect_status_prefix(testname, name, prefix);
}

static void expect_rel_position_status(const char *testname, IecDrive *dr, uint8_t chan,
                                       uint16_t record, uint8_t offset,
                                       const char *expected)
{
    uint8_t cmd[5] = {
        'P',
        chan,
        (uint8_t)(record & 0xFF),
        (uint8_t)(record >> 8),
        offset
    };
    send_command_data(dr, cmd, sizeof(cmd));
    get_status(dr);
    if (strcmp(last_status, expected) != 0) {
        printf("%s: P channel %u record %u offset %u: status was '%s', expected '%s'\n",
               testname, chan, record, offset, last_status, expected);
    }
    REQUIRE(strcmp(last_status, expected) == 0);
}

static void expect_rel_write(const char *testname, IecDrive *dr, uint8_t chan,
                             const uint8_t *data, int len)
{
    send_channel_data(dr, chan, data, len);
    get_status(dr);
    expect_status_ok(testname, "REL write");
}

static void expect_rel_read(const char *testname, IecDrive *dr, uint8_t chan,
                            const uint8_t *expected, int expected_len)
{
    uint8_t buffer[256];
    memset(buffer, 0, sizeof(buffer));

    int got = read_file(dr, chan, buffer, sizeof(buffer));
    if (got != expected_len || memcmp(buffer, expected, expected_len) != 0) {
        printf("%s: REL read channel %u: got %d bytes, expected %d bytes\n",
               testname, chan, got, expected_len);
        dump_hex_relative(buffer, got);
    }
    REQUIRE(got == expected_len);
    REQUIRE(memcmp(buffer, expected, expected_len) == 0);
}

static void expect_short_read(const char *testname, IecDrive *dr, uint8_t chan, int readlen,
                            const uint8_t *expected, int expected_len)
{
    uint8_t buffer[256];
    memset(buffer, 0, sizeof(buffer));

    int got = read_file(dr, chan, buffer, readlen);
    if (got != expected_len || memcmp(buffer, expected, expected_len) != 0) {
        printf("%s: REL read channel %u: got %d bytes, expected %d bytes\n",
               testname, chan, got, expected_len);
        dump_hex_relative(buffer, got);
    }
    REQUIRE(got == expected_len);
    REQUIRE(memcmp(buffer, expected, expected_len) == 0);
}

static void expect_rel_read_bytes_individually(const char *testname, IecDrive *dr, uint8_t chan,
                                               const uint8_t *expected, int expected_len)
{
    uint8_t buffer[256];
    memset(buffer, 0, sizeof(buffer));

    for (int i = 0; i < expected_len; i++) {
        int got = read_file_limited(dr, chan, buffer + i, 1);
        if (got != 1) {
            printf("%s: REL single-byte read %d on channel %u got %d bytes\n",
                   testname, i, chan, got);
        }
        REQUIRE(got == 1);
    }

    if (memcmp(buffer, expected, expected_len) != 0) {
        printf("%s: REL single-byte stream mismatch, expected %d bytes\n",
               testname, expected_len);
        dump_hex_relative(buffer, expected_len);
    }
    REQUIRE(memcmp(buffer, expected, expected_len) == 0);
}

void read_directory(IecDrive *dr, const char *file)
{
    open_file(dr, file);
    get_status(dr);

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x60);
    dr->talk();
    int ret;
    uint8_t *data;
    int data_size;
    do {
        ret = dr->prefetch_more(256, data, data_size);
        // printf("Ret: %d Count = %d\n", ret, data_size);
        dump_hex_relative(data, data_size);
        dr->pop_more(data_size);
    } while(ret == 0);

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0xE0); // close

    get_status(dr);
}

const uint8_t *testmsg = (const uint8_t *)"This is really a silly test.";
void create_test_files(FileManager *fm)
{
    uint32_t tr;
    FRESULT fres;
    fres = fm->save_file(false, "/Temp", "a.prg", testmsg, 28, &tr);
    if (fres == FR_OK) {
        printf("Written %u bytes to the file.\n", tr);
    } else {
        printf("%s\n", FileSystem::get_error_string(fres));
    }
    fm->save_file(false, "/Temp", "bb.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "ccc.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "cc2.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "cc3.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "dddd.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "eeeee.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "ffffff.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "ggggggg.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "hhhhhhhh.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "iiiiiiiii.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "jjjjjjjjjj.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "kkkkkkkkkkk.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "llllllllllll.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "mmmmmmmmmmmmm.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "nnnnnnnnnnnnnn.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "ooooooooooooooo.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "pppppppppppppppp.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "qqqqqqqq012345678.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "abc{2f}def.usr", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "{c1c2c3}.seq", testmsg, 28, &tr);
    fm->create_dir("/Temp/SomeDir");
    fm->create_dir("/Temp/OtherDir");
    fm->create_dir("/Temp/OtherDir/Level2");
    fm->create_dir("/Temp/blah{c1c2}");
}

static void run_iec_partition3_sequence(IecDrive *dr, const char *label)
{
    printf("\nRunning IEC partition 3 sequence on %s\n", label);

    expect_iec_file("TEST01", dr, 0, "3:BASIC", "BASIC:PRG");
    expect_iec_file("TEST02", dr, 2, "3:BASIC,P,R", "BASIC:PRG");
    expect_iec_file("TEST03", dr, 2, "3:BASIC,S,R", "BASIC:SEQ");

    expect_iec_file("TEST04", dr, 0, "3:LITERAL", "LITERAL:PRG");
    expect_iec_file("TEST05", dr, 0, "3:LITERAL.PRG", "LITERAL.PRG:PRG");
    expect_iec_file("TEST06", dr, 2, "3:LITERAL.PRG,P,R", "LITERAL.PRG:PRG");
    expect_iec_file("TEST07", dr, 2, "3:LITERAL.PRG,S,R", "LITERAL.PRG:SEQ");

    expect_iec_file_missing("TEST08", dr, 0, "3:ONLYBASE.PRG");

    expect_iec_file("TEST09", dr, 0,
        "3:Q.\xC6\xC3\xC4\xC3\xC6\xC3\xC4\xC3\xC6\xC3\xC4\xC3\xC6\xC3",
        "NASTY:PRG");

    expect_iec_file("TEST10", dr, 2, "3:BASIC", "BASIC:PRG");
    expect_iec_file("TEST11", dr, 2, "3:LITERAL.PRG", "LITERAL.PRG:PRG");
    expect_iec_file_missing("TEST12", dr, 2, "3:BASIC,U");

    expect_command_response("TEST13", dr, "CP3", "02,PARTITION SELECTED,03,00\r");
    expect_command_response("TEST14", dr, "XPWD", "3:/");
    expect_command_response("TEST15", dr, "T-RI", "2025-06-26T00:41:01 WED\r");
    expect_command_response("TEST16", dr, "C3:BAD=", "34,SYNTAX ERROR,00,00\r");

    expect_command_ok("TEST17", dr, "C3:COMBO=3:BASIC,3:LITERAL");
    expect_iec_file("TEST18", dr, 0, "3:COMBO", "BASIC:PRGLITERAL:PRG");

    expect_command_ok("TEST19", dr, "R3:RENAMED=3:BASIC");

    expect_iec_file("TEST20", dr, 0, "3:RENAMED", "BASIC:PRG");
    expect_iec_file("TEST21", dr, 2, "3:BASIC", "BASIC:SEQ");

    expect_command_response("TEST22", dr, "S3:RENAMED", "01, FILES SCRATCHED,01,00\r");
    expect_iec_file_missing("TEST23", dr, 0, "3:RENAMED");

    expect_command_response("TEST24", dr, "S3:LITERAL*", "01, FILES SCRATCHED,03,00\r");
    expect_iec_file_missing("TEST25", dr, 0, "3:LITERAL");
    expect_iec_file_missing("TEST26", dr, 0, "3:LITERAL.PRG");

    printf("IEC partition 3 sequence on %s completed successfully!\n", label);
}

// SI-070, SI-002, SI-005, SI-010, SI-011: the type and access suffixes of an open, on a
// partition rooted in a host directory and in an image.
void execute_suite3(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite3";
    const char *fat_path = "/Fat/iec_fat_cases";
    const MountedImageCase cases[] = {
        { create_iec_d64_fixture, "output/iec_corner_cases.d64", "/Temp/iec_corner_cases.d64", "D64" },
        { create_iec_d81_fixture, "output/iec_corner_cases.d81", "/Temp/iec_corner_cases.d81", "D81" },
    };

    dr->add_partition(1, "/Temp", "RAMDISK");
    run_image_matrix(testname, "Partition 3 file-type matrix on D64/D81/FAT", cases,
        [&](const MountedImageCase &c) {
            prepare_disk_partition(c.source, c.mount, dr, 3, c.label);
            run_iec_partition3_sequence(dr, c.label);
        });
    create_iec_fat_fixture(fm, fat_path);
    dr->add_partition(3, fat_path, "FAT");
    run_iec_partition3_sequence(dr, "FAT");
}

static void run_iec_rel_sequence(IecDrive *dr, const char *testname)
{
    const uint8_t chan = 5;
    static const int record_size = 40;

    uint8_t record1[record_size];
    uint8_t expected[record_size];
    uint8_t gap[record_size];
    uint8_t stream_expected[record_size];
    uint8_t cr = 0x0D;
    const uint8_t patch[] = "xy";
    const uint8_t tail[] = "TAIL";

    memcpy(record1, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcd", record_size);
    memcpy(expected, record1, record_size);
    memset(gap, 0, sizeof(gap));
    gap[0] = 0xFF;

    // Issue #655: COPY-ALL writes records without a P command and reuses the channel.
    // Check the native header and exact contents before reopening can alter the file.
    const char *copy_names[] = { "4:COPYFIRST", "4:COPYSECOND", "4:COPYBOUNDARY" };
    const char *copy_files[] = { "COPYFIRST.rel", "COPYSECOND.rel", "COPYBOUNDARY.rel" };
    const uint8_t copy_sizes[] = { 164, 31, 254 };
    FileManager *fm = FileManager::getFileManager();
    for (int file = 0; file < 3; file++) {
        const int size = copy_sizes[file];
        uint8_t records[2][254];
        memset(records[0], 'A' + file, sizeof(records[0]));
        memset(records[1], 'a' + file, sizeof(records[1]));
        expect_rel_open("Suite4-CopyCreate", dr, chan, copy_names[file], size);
        expect_rel_write("Suite4-CopyFirstRecord", dr, chan, records[0], size);
        expect_rel_write("Suite4-CopySecondRecord", dr, chan, records[1], size - 3);
        close_file(dr, chan);
        expect_status_ok("Suite4-CopyClose", copy_names[file]);
        memset(records[1] + size - 3, 0, 3);

        File *verify = NULL;
        REQUIRE(fm->fopen(dr->get_partition_dir(4), copy_files[file], FA_READ, &verify) == FR_OK);
        const uint32_t expected_size = 2 + 2 * size;
        printf("%s: %s: size %u, expected %u\n", testname, copy_names[file],
               verify->get_size(), expected_size);
        REQUIRE(verify->get_size() == expected_size);
        uint8_t actual[510];
        uint32_t got = 0;
        REQUIRE(verify->read(actual, sizeof(actual), &got) == FR_OK);
        fm->fclose(verify);
        REQUIRE(got == expected_size);
        REQUIRE(actual[0] == size && actual[1] == 0);
        REQUIRE(memcmp(actual + 2, records[0], size) == 0);
        REQUIRE(memcmp(actual + 2 + size, records[1], size) == 0);

        // The supplied editor opens by name alone, then positions the REL record.
        open_file(dr, chan, copy_names[file]);
        get_status(dr);
        expect_status_ok("Suite4-CopyReopenUntyped", copy_names[file]);
        expect_rel_position_status("Suite4-CopyPositionUntyped", dr, chan, 1, 1, "00, OK,00,00\r");
        expect_rel_write("Suite4-CopyUpdateUntyped", dr, chan, records[0], size);
        expect_rel_position_status("Suite4-CopyRepositionUntyped", dr, chan, 1, 1, "00, OK,00,00\r");
        expect_rel_read("Suite4-CopyReadFirst", dr, chan, records[0], size);
        expect_rel_read("Suite4-CopyReadSecond", dr, chan, records[1], size - 3);
        close_file(dr, chan);
    }

    expect_rel_open("Suite4-CreateRel", dr, chan, "4:RELTEST", record_size);
    expect_rel_position_status("Suite4-PositionRecord1", dr, chan, 1, 1, "50,RECORD NOT PRESENT,00,00\r");
    expect_rel_write("Suite4-WriteRecord1", dr, chan, record1, record_size);
    close_file(dr, chan);
    expect_status_ok("Suite4-CloseAfterCreate", "4:RELTEST");

    // copy_from(d64_path, diskname); // Use this to analyze what's on the disk until a certain point (debug)

    expect_rel_open("Suite4-ReopenRel", dr, chan, "4:RELTEST", record_size);
    expect_rel_position_status("Suite4-PositionRecord1Read", dr, chan, 1, 1, "00, OK,00,00\r");
    expect_rel_read("Suite4-ReadRecord1", dr, chan, record1, record_size);

    expect_rel_position_status("Suite4-PositionRecord1Patch", dr, chan, 1, 11, "00, OK,00,00\r");
    expect_rel_write("Suite4-ShortOverwrite", dr, chan, patch, sizeof(patch) - 1);
    expected[10] = 'x';
    expected[11] = 'y';
    expect_rel_position_status("Suite4-PositionRecord1AfterPatch", dr, chan, 1, 1, "00, OK,00,00\r");
    expect_rel_read("Suite4-ReadShortOverwrite", dr, chan, expected, 12); // not record_size, as it got truncated

    expect_rel_position_status("Suite4-PositionRecord3Write", dr, chan, 3, 1, "50,RECORD NOT PRESENT,00,00\r");
    expect_rel_write("Suite4-ExtendRecord3", dr, chan, tail, sizeof(tail) - 1);
    expect_rel_position_status("Suite4-PositionGapRecord", dr, chan, 2, 1, "00, OK,00,00\r");
    expect_rel_read("Suite4-ReadGapRecord", dr, chan, gap, 1); // only expect one 0xFF to be returned

    expect_rel_position_status("Suite4-PositionBeyondEof", dr, chan, 4, 1, "50,RECORD NOT PRESENT,00,00\r");
    expect_rel_read("Suite4-ReadBeyondEof", dr, chan, gap, 1);
    get_status(dr);
    expect_current_status("Suite4-StatusBeyondEof", "4:RELTEST", "00, OK,00,00\r");

    close_file(dr, chan);
    expect_status_ok("Suite4-CloseRel", "4:RELTEST");

    int stream_len = 0;
    memcpy(stream_expected + stream_len, expected, 12);
    stream_len += 12;
    stream_expected[stream_len++] = 0xFF;
    memcpy(stream_expected + stream_len, tail, sizeof(tail) - 1);
    stream_len += sizeof(tail) - 1;
    stream_expected[stream_len++] = 0xFF;

    expect_rel_open_status_prefix("Suite4-OpenExistingRelWithNoRecordSize", dr, chan, "4:RELTEST", 0, "00, OK");
    expect_short_read("Suite4-SequentialReadRecord1a", dr, chan, 4, expected, 4);
    expect_short_read("Suite4-SequentialReadRecord1b", dr, chan, 4, expected+4, 4);
    expect_short_read("Suite4-SequentialReadRecord1c", dr, chan, 4, expected+8, 4);
    expect_rel_read("Suite4-SequentialReadRecord2", dr, chan, gap, 1);
    expect_rel_read("Suite4-SequentialReadRecord3", dr, chan, tail, sizeof(tail) - 1);
    expect_rel_read("Suite4-SequentialReadRecord4", dr, chan, gap, 1);
    close_file(dr, chan);
    expect_status_ok("Suite4-CloseRelNoRecordSize", "4:RELTEST");

    expect_rel_open_status_prefix("Suite4-OpenExistingRelWithNoRecordSizeByteReads", dr, chan, "4:RELTEST", 0, "00, OK");
    expect_rel_read_bytes_individually("Suite4-SingleByteSequentialReads", dr, chan, stream_expected, stream_len);
    close_file(dr, chan);
    expect_status_ok("Suite4-CloseRelAfterSingleByteReads", "4:RELTEST");

    expect_rel_open_status_prefix("Suite4-RecordSizeMismatch", dr, chan, "4:RELTEST", record_size - 1, "50,RECORD NOT PRESENT");
    close_file(dr, chan);

    expect_rel_open_status_prefix("Suite4-CreateRelWithNoRecordSize", dr, chan, "4:RELTEST2", 0, "62,FILE NOT FOUND");
    close_file(dr, chan);

    printf("%s completed successfully!\n", testname);
}

// SI-080, SI-081, SI-082: a relative file opened with a record length, positioned by P
// on every medium.
void execute_suite4(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite4";
    const char *fat_path = "/Fat/iec_rel_cases_fat";
    const MountedImageCase cases[] = {
        { create_iec_d64_fixture, "output/iec_rel_cases.d64", "/Temp/iec_rel_cases.d64", "REL Suite on D64" },
        { create_iec_d81_fixture, "output/iec_rel_cases.d81", "/Temp/iec_rel_cases.d81", "REL Suite on D81" },
    };

    run_image_matrix(testname, "REL file matrix on D64/D81/FAT", cases,
        [&](const MountedImageCase &c) {
            prepare_disk_partition(c.source, c.mount, dr, 4, "REL");
            run_iec_rel_sequence(dr, c.label);
        });
    prepare_fat_partition(fm, dr, fat_path, 4, "FAT");
    run_iec_rel_sequence(dr, "REL Suite on FAT");
}

static void run_iec_append_replace_sequence(IecDrive *dr, const char *label)
{
    const uint8_t chan = 6;

    printf("\nRunning append/replace sequence on %s\n", label);

    expect_iec_write_ok("Suite5-CreateSeq", dr, chan, "5:APPENDME,S,W", "alpha");
    expect_iec_file("Suite5-ReadCreatedSeq", dr, chan, "5:APPENDME,S,R", "alpha");

    expect_iec_write_ok("Suite5-AppendExplicitSeq", dr, chan, "5:APPENDME,S,A", "+beta");
    expect_iec_file("Suite5-ReadExplicitAppend", dr, chan, "5:APPENDME,S,R", "alpha+beta");

    expect_iec_write_ok("Suite5-AppendDefaultSeq", dr, chan, "5:APPENDME,A", "+gamma");
    expect_iec_file("Suite5-ReadDefaultAppend", dr, chan, "5:APPENDME,S,R", "alpha+beta+gamma");

    expect_iec_open_status_prefix("Suite5-AppendMissing", dr, chan, "5:MISSING,S,A", "62,FILE NOT FOUND");

    expect_iec_open_status_prefix("Suite5-WriteExistingNoReplace", dr, chan, "5:APPENDME,S,W", "63,FILE EXISTS");
    expect_iec_file("Suite5-ReadAfterFailedWrite", dr, chan, "5:APPENDME,S,R", "alpha+beta+gamma");

    expect_iec_write_ok("Suite5-ReplaceExisting", dr, chan, "@5:APPENDME,S,W", "replaced");
    expect_iec_file("Suite5-ReadReplaced", dr, chan, "5:APPENDME,S,R", "replaced");

    expect_iec_write_ok("Suite5-AppendAfterReplace", dr, chan, "5:APPENDME,S,A", "+tail");
    expect_iec_file("Suite5-ReadAppendAfterReplace", dr, chan, "5:APPENDME,S,R", "replaced+tail");

    expect_iec_write_ok("Suite5-ReplaceCreatesMissing", dr, chan, "@5:CREATED,S,W", "created");
    expect_iec_file("Suite5-ReadReplaceCreated", dr, chan, "5:CREATED,S,R", "created");

    printf("Append/replace sequence on %s completed successfully!\n", label);
}

// SI-035: an open for writing when a file of that name exists answers 63, an open for
// reading when none does answers 62, and an existing REL opened with another type 64.
void execute_suite5(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite5";
    const char *fat_path = "/Fat/iec_append_replace_cases";
    const MountedImageCase cases[] = {
        { create_iec_d64_fixture, "output/iec_append_replace_cases.d64", "/Temp/iec_append_replace_cases.d64", "D64" },
        { create_iec_d81_fixture, "output/iec_append_replace_cases.d81", "/Temp/iec_append_replace_cases.d81", "D81" },
    };

    run_image_matrix(testname, "Append/replace matrix on D64/D81/FAT", cases,
        [&](const MountedImageCase &c) {
            prepare_disk_partition(c.source, c.mount, dr, 5, "APPEND");
            run_iec_append_replace_sequence(dr, c.label);
        });
    prepare_fat_partition(fm, dr, fat_path, 5, "APPEND");
    run_iec_append_replace_sequence(dr, "FAT");
}

void execute_suite6(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite6";
    const char *fat_path = "/Fat/iec_fat_accessibility_cases";
    FRESULT fres;

    print_scenario(testname, "Rendered-name fallback and directory-assisted access on FAT");
    prepare_fat_partition(fm, dr, fat_path, 6, "ACCESS");

    save_fixture_file(fm, fat_path, "JOHN.DOE", "JOHN:DOE");
    save_fixture_file(fm, fat_path, "MYPROGRAM", "NOEXT");
    save_fixture_file(fm, fat_path, "A{41}B.prg", "ESCAPED");
    save_fixture_file(fm, fat_path, "1234567890123456ZZ.prg", "LONGNAME");
    save_fixture_file(fm, fat_path, "PRIORITY.prg", "CANONICAL");
    save_fixture_file(fm, fat_path, "PRIORITY", "DIRECT");

    fres = fm->create_dir("/Fat/iec_fat_accessibility_cases/A{44}IR");
    REQUIRE(fres == FR_OK || fres == FR_EXIST);
    save_fixture_file(fm, "/Fat/iec_fat_accessibility_cases/A{44}IR", "INNER.prg", "INNER");

    fres = fm->create_dir("/Fat/iec_fat_accessibility_cases/P{41}RENT");
    REQUIRE(fres == FR_OK || fres == FR_EXIST);

    fres = fm->create_dir("/Fat/iec_fat_accessibility_cases/R{44}IR");
    REQUIRE(fres == FR_OK || fres == FR_EXIST);

    save_fixture_file(fm, fat_path, "C{4f}PYSRC.prg", "COPY");
    save_fixture_file(fm, fat_path, "R{45}NAMESRC.prg", "RENAME");
    save_fixture_file(fm, fat_path, "S{43}RATCHME.prg", "SCRATCH");

    expect_iec_file("Suite6-NonCbmExtensionAny", dr, 2, "6:JOHN.DOE", "JOHN:DOE");
    expect_iec_file("Suite6-NonCbmExtensionLoad", dr, 0, "6:JOHN.DOE", "JOHN:DOE");
    expect_iec_file("Suite6-NoExtension", dr, 0, "6:MYPROGRAM", "NOEXT");
    expect_iec_file("Suite6-EscapeLookingFatName", dr, 0, "6:AAB,P,R", "ESCAPED");
    expect_iec_file("Suite6-TruncatedRenderedName", dr, 0, "6:1234567890123456,P,R", "LONGNAME");
    expect_iec_file("Suite6-CanonicalNamePriority", dr, 0, "6:PRIORITY", "CANONICAL");
    expect_iec_file("Suite6-ReadThroughRenderedDirPath", dr, 0, "6/ADIR:INNER", "INNER");

    expect_directory_contains("Suite6-DirectoryRenderedPath", dr, "$6/ADIR", "INNER");
    expect_command_ok("Suite6-CDRenderedPath", dr, "CD6/ADIR");
    expect_iec_file("Suite6-ReadAfterCDRenderedPath", dr, 0, "6:INNER", "INNER");
    expect_command_ok("Suite6-CDRoot", dr, "CD6//");

    expect_command_ok("Suite6-MDRenderedParentPath", dr, "MD6/PARENT:CHILD");
    expect_command_ok("Suite6-CDRenderedParentCreatedChild", dr, "CD6/PARENT/CHILD");
    expect_command_ok("Suite6-CDRootAfterMD", dr, "CD6//");
    // RD takes no path (SI-063), so the parent is entered first.
    expect_command_ok("Suite6-CDRenderedParentForRD", dr, "CD6/PARENT");
    expect_command_ok("Suite6-RDRenderedParentPath", dr, "RD6:CHILD");
    expect_command_ok("Suite6-CDRootAfterRD", dr, "CD6//");
    expect_command_ok("Suite6-RDRenderedFinalDirectory", dr, "RD6:RDIR");

    expect_command_ok("Suite6-CopyRenderedSource", dr, "C6:COPYDST=6:COPYSRC");
    expect_iec_file("Suite6-ReadCopiedRenderedSource", dr, 0, "6:COPYDST", "COPY");

    expect_command_ok("Suite6-RenameRenderedSource", dr, "R6:RENAMED=6:RENAMESRC");
    expect_iec_file("Suite6-ReadRenamedRenderedSource", dr, 0, "6:RENAMED", "RENAME");
    expect_iec_file_missing("Suite6-RenamedRenderedSourceMissing", dr, 0, "6:RENAMESRC");

    expect_command_response("Suite6-ScratchRenderedSource", dr, "S6:SCRATCHME",
                            "01, FILES SCRATCHED,01,00\r");
    expect_iec_file_missing("Suite6-ScratchedRenderedSourceMissing", dr, 0, "6:SCRATCHME");

    printf("Suite6 completed successfully!\n");
}

void execute_suite7(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite7";
    const char *fat_path = "/Fat/iec_fixes_1_to_4";

    print_scenario(testname, "Parser, seek, empty-file, and partition-guard fixes");
    prepare_fat_partition(fm, dr, fat_path, 7, "FIXES");

    save_fixture_file(fm, fat_path, "SEEKME.prg", "0123456789");
    save_fixture_file(fm, fat_path, "EMPTY.prg", "");
    save_fixture_file(fm, fat_path, "BAD.prg", "BAD");

    dr->add_partition(999, fat_path, "INVALID");

    expect_iec_open_status_prefix("Suite7-OpenBadModifier", dr, 0, "7:BAD,X", "30,SYNTAX ERROR");
    expect_iec_open_status_prefix("Suite7-OpenBadRecordType", dr, 0, "7:BAD,P,L", "30,SYNTAX ERROR");
    expect_iec_open_status_prefix("Suite7-OpenMalformedReplace", dr, 0, "@345:", "34,SYNTAX ERROR");
    expect_iec_open_status_prefix("Suite7-OpenMalformedDollar", dr, 0, "$", "00");
    //expect_iec_open_status_prefix("Suite7-OpenMalformedHash", dr, 0, "#", "30,SYNTAX ERROR");

    expect_command_response("Suite7-CP999", dr, "CP999", "77,SELECTED PARTITION ILLEGAL,999,00\r");
    expect_iec_open_status_prefix("Suite7-OpenInvalidPartition", dr, 0, "999:SEEKME", "77,SELECTED PARTITION ILLEGAL");

    expect_iec_seek_read("Suite7-SeekNormalFile", dr, 2, "7:SEEKME", 3, "3456789");
    expect_iec_file("Suite7-EmptyAfterNonEmpty", dr, 2, "7:EMPTY", "");

    printf("Suite7 completed successfully!\n");
}

static void run_suite8_control_plane(FileManager *fm, IecDrive *dr)
{
    const char *suite = "Suite8";
    const char *testname = "Suite8";
    const char *msg = (const char *)testmsg;
    FRESULT fres;
    uint32_t tr;

    print_scenario(suite, "Control-plane routing and directory mutation");
    create_test_files(fm);
    fres = fm->create_dir("/Temp/Partition2");
    REQUIRE(fres == FR_OK || fres == FR_EXIST);
    dr->add_partition(1, "/Temp", "RAMDISK");
    dr->add_partition(2, "/Temp/Partition2", "PART2");

    expect_command_response("Suite8-CP1", dr, "CP1", "02,PARTITION SELECTED,01,00\r");
    expect_command_response("Suite8-CD-TEMP", dr, "CD/TEMP", "71,DIRECTORY ERROR,01,00\r");
    expect_command_ok("Suite8-CD-SOMEDIR", dr, "CD/SOMEDIR");
    // Up and down again in one command is spelled with .., because a left arrow in a
    // path component is a directory name (SI-014).
    expect_command_ok("Suite8-CD-PARENT-OTHERDIR", dr, "CD/../OTHERDIR");
    expect_command_ok("Suite8-CD-LEVEL2", dr, "CD:LEVEL2");
    expect_command_ok("Suite8-CD-ROOT", dr, "CD//");
    expect_command_ok("Suite8-CD-ABS-LEVEL2", dr, "CD/OTHERDIR/LEVEL2");
    expect_command_ok("Suite8-CD-PETSCII", dr, "CD//:BLAH\xc1\xc2");
    expect_command_ok("Suite8-CD-PARENT", dr, "CD_");
    expect_directory_read("Suite8-DIR-TIME-LONG", dr, "$=T0:*=L");
    expect_command_ok("Suite8-MD-HOI", dr, "MD:HOI");
    expect_command_ok("Suite8-MD-OTHERDIR-DEEPER", dr, "MD/OTHERDIR:DEEPER");
    expect_command_response("Suite8-MD-BAD-PARENT", dr, "MD_/OTHERDIR:DEEPER", "71,DIRECTORY ERROR,00,00\r");
    expect_command_ok("Suite8-MD-PARENT-DEEPER", dr, "MD:_/DEEPER");
    expect_command_ok("Suite8-RD-HOI", dr, "RD:HOI");
    expect_command_ok("Suite8-MD-HOI-AGAIN", dr, "MD:HOI");

    fres = fm->save_file(false, "/Temp/HOI", "mmmmmmmmmmmmm.prg", testmsg, 28, &tr);
    expect_fresult("Suite8-SAVE-NONEMPTY-DIR", "/Temp/HOI/mmmmmmmmmmmmm.prg", fres, FR_OK);
    expect_transferred("Suite8-SAVE-NONEMPTY-DIR", "/Temp/HOI/mmmmmmmmmmmmm.prg", tr, 28);
    expect_command_response("Suite8-RD-NONEMPTY-HOI", dr, "RD:HOI", "63,FILE EXISTS,00,00\r");
}

// SI-073, SI-075, SI-120: scratch, copy and the clock.
static void run_suite8_time_copy_rename_scratch(IecDrive *dr)
{
    const char *suite = "Suite8";
    const char *msg = (const char *)testmsg;
    const char *write_msg = "This is some random string that should be written to an open file.";

    print_scenario(suite, "Time, copy, rename, scratch");
    // Month first: 26 June 2025, not the 6th of the 26th month.
    expect_command_response("Suite8-T-RA", dr, "T-RA", "WED. 06/26/25 12:41:01 AM\r");
    expect_command_response("Suite8-T-RI", dr, "T-RI", "2025-06-26T00:41:01 WED\r");
    static const uint8_t t_rd[] = { 3, 125, 6, 26, 12, 41, 1, 0, 0x0d };
    static const uint8_t t_rb[] = { 3, 0x25, 0x06, 0x26, 0x12, 0x41, 0x01, 0, 0x0d };
    expect_command_bytes("Suite8-T-RD", dr, "T-RD", t_rd, sizeof(t_rd));
    expect_command_bytes("Suite8-T-RB", dr, "T-RB", t_rb, sizeof(t_rb));

    // SI-120. A clock write reaches the system clock, which is the one every other
    // interface reads, and a write the clock cannot hold answers 30 and changes nothing.
    // The four formats and their validation are checked in target/pc/linux/parse.
    expect_command_ok("Suite8-T-WA", dr, "T-WA" "SAT. 09/12/26 01:02:03 PM");
    expect_command_response("Suite8-T-WA-READ", dr, "T-RI", "2026-09-12T13:02:03 SAT\r");
    {
        const char *testname = "Suite8-T-WA-SYSTEM-CLOCK";
        int wd, year, month, day, hour, min, sec;
        get_current_time(wd, year, month, day, hour, min, sec);
        if ((wd != 6) || (year != 2026) || (month != 9) || (day != 12) ||
            (hour != 13) || (min != 2) || (sec != 3)) {
            printf("%s: the system clock reads %d-%02d-%02d %02d:%02d:%02d, day of week %d\n",
                   testname, year, month, day, hour, min, sec, wd);
        }
        REQUIRE((wd == 6) && (year == 2026) && (month == 9) && (day == 12) &&
                (hour == 13) && (min == 2) && (sec == 3));
    }
    expect_command_response("Suite8-T-W-INVALID", dr, "T-WI2026-02-30T00:00:00",
                            "30,SYNTAX ERROR,00,00\r");
    expect_command_response("Suite8-T-W-INVALID-READ", dr, "T-RI", "2026-09-12T13:02:03 SAT\r");
    // Leave the clock where the rest of the suite expects it. Its day of week is not the
    // one the date has, so it is written in a form that carries one.
    static const uint8_t t_wd_restore[] = { 'T','-','W','D', 3, 125, 6, 26, 12, 41, 1, 0 };
    expect_command_data_response("Suite8-T-W-RESTORE", dr, t_wd_restore, sizeof(t_wd_restore),
                                 "00, OK,00,00\r");
    expect_command_response("Suite8-T-W-RESTORE-READ", dr, "T-RI", "2025-06-26T00:41:01 WED\r");

    expect_command_response("Suite8-COPY-MISSING-SOURCE", dr, "C2:DEST=", "34,SYNTAX ERROR,00,00\r");
    expect_command_ok("Suite8-COPY-A-BB", dr, "C2:DEST=1:A,1:BB");
    expect_iec_file("Suite8-COPY-DEST", dr, 0, "2:DEST", "This is really a silly test.This is really a silly test.");

    expect_command_response("Suite8-CP2", dr, "CP2", "02,PARTITION SELECTED,02,00\r");
    expect_command_ok("Suite8-RENAME-P1", dr, "RENAME1:DOOM=1:DDDD");
    expect_iec_file("Suite8-RENAME-P1-CHECK", dr, 0, "1:DOOM", msg);
    expect_command_ok("Suite8-RENAME-P1-TO-P2", dr, "RENAME2:DOOM=1:DOOM");
    expect_iec_file("Suite8-RENAME-P2-CHECK", dr, 0, "2:DOOM", msg);
    expect_iec_file_missing("Suite8-RENAME-P1-MISSING", dr, 0, "1:DOOM");
    expect_directory_read("Suite8-DIR-P1", dr, "$1");
    expect_command_response("Suite8-SCRATCH-C-WILDCARD", dr, "SCRATCH1:C*", "01, FILES SCRATCHED,03,00\r");
    expect_iec_file_missing("Suite8-SCRATCH-CCC-MISSING", dr, 0, "1:CCC");
    expect_iec_file("Suite8-READ-A", dr, 0, "1:A", msg);
    write_file(dr, 3, "2:WRITETEST,U,W", write_msg);
    expect_iec_file("Suite8-WRITETEST", dr, 2, "2:WRITETEST,U,R", write_msg);
}

static void run_suite8_partition_listing(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite8";
    FRESULT fres;
    uint32_t tr;

    print_scenario("Suite8", "Partition listing and case-insensitive subdirectories");
    dr->add_partition(11, "/Temp", "P11");
    expect_directory_read("Suite8-DIR-PARTITIONS", dr, "$=P");
    expect_command_response("Suite8-BINARY-CP11", dr, "C\xD0\x0B", "02,PARTITION SELECTED,11,00\r");

    fres = fm->save_file(true, "/Temp", "ccc.prg", testmsg, 28, &tr);
    expect_fresult("Suite8-SAVE-CCC", "/Temp/ccc.prg", fres, FR_OK);
    expect_transferred("Suite8-SAVE-CCC", "/Temp/ccc.prg", tr, 28);
    fres = fm->save_file(true, "/Temp", "cc2.prg", testmsg, 28, &tr);
    expect_fresult("Suite8-SAVE-CC2", "/Temp/cc2.prg", fres, FR_OK);
    expect_transferred("Suite8-SAVE-CC2", "/Temp/cc2.prg", tr, 28);
    fres = fm->save_file(true, "/Temp", "abc.prg", testmsg, 28, &tr);
    expect_fresult("Suite8-SAVE-ABC", "/Temp/abc.prg", fres, FR_OK);
    expect_transferred("Suite8-SAVE-ABC", "/Temp/abc.prg", tr, 28);

    fres = fm->create_dir("/Temp/Subdir");
    REQUIRE(fres == FR_OK || fres == FR_EXIST);
    fres = fm->save_file(true, "/Temp/SubDir", "01234.usr", testmsg, 28, &tr);
    expect_fresult("Suite8-SAVE-SUBDIR-USR", "/Temp/SubDir/01234.usr", fres, FR_OK);
    expect_transferred("Suite8-SAVE-SUBDIR-USR", "/Temp/SubDir/01234.usr", tr, 28);

    expect_directory_read("Suite8-DIR-FOLDERS", dr, "$:*=B");
    expect_directory_read("Suite8-DIR-SUBDIR", dr, "$/Subdir");
    expect_command_ok("Suite8-CD-SUBDIR", dr, "CD/Subdir");
    expect_command_response("Suite8-XPWD-SUBDIR", dr, "XPWD", "11:/SUBDIR/");
    expect_directory_read("Suite8-DIR-ROOT", dr, "$//");
    expect_directory_read("Suite8-DIR-PARENT", dr, "$/../");
}

void execute_suite8(FileManager *fm, IecDrive *dr)
{
    run_suite8_control_plane(fm, dr);
    run_suite8_time_copy_rename_scratch(dr);
    run_suite8_partition_listing(fm, dr);

    printf("Suite8 completed successfully!\n");
}

static void run_suite9_block_matrix(FileManager *fm, IecDrive *dr)
{
    const char *suite = "Suite9";
    const char *testname = "Suite9";
    const BlockCase cases[] = {
        { create_iec_d64_fixture, "output/iec_block_cases.d64", "/Temp/iec_block_cases.d64", "D64", 9, 17, 0, 17, 10, 18, 0, false },
        { create_iec_d81_fixture, "output/iec_block_cases.d81", "/Temp/iec_block_cases.d81", "D81", 9, 39, 0, 39, 10, 40, 1, true },
    };
    const char *fat_path = "/Fat/iec_block_cases_fat";

    print_scenario(suite, "Block read/write/allocate/free on D64/D81");
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const BlockCase &c = cases[i];
        c.create_fixture(c.source);
        prepare_disk_partition(c.source, c.mount, dr, c.partition, c.label);
        char select[16];
        snprintf(select, sizeof(select), "CP%d", c.partition);
        expect_command_status_prefix("Suite9-SelectPartition", dr, select, "02,PARTITION SELECTED");

        uint8_t expected_read[256];
        make_sector_payload(expected_read, "BASIC:PRG");
        open_buffer_channel("Suite9-OpenBufferRead", dr, 2);
        capture_block_sector("Suite9-BlockRead", dr, 2, c.partition, c.read_track, c.read_sector, expected_read);
        close_file(dr, 2);
        expect_status_ok("Suite9-CloseBufferRead", "#2");

        uint8_t write_payload[256];
        make_increment_pattern(write_payload);
        open_buffer_channel("Suite9-OpenBufferWrite", dr, 2);
        expect_command_ok("Suite9-RewindBuffer", dr, "B-P 2 0"); // a new buffer starts at byte 1 (SI-090)
        send_channel_data(dr, 2, write_payload, sizeof(write_payload));
        expect_block_write_sector("Suite9-BlockWrite", dr, 2, c.partition, c.write_track, c.write_sector, write_payload);
        close_file(dr, 2);
        expect_status_ok("Suite9-CloseBufferWrite", "#2");

        uint32_t free_before = get_free_sectors(fm, c.mount);

        expect_command_ok("Suite9-BlockAllocate", dr, "B-A 9 17 10");
        uint32_t free_after_alloc = get_free_sectors(fm, c.mount);
        REQUIRE(free_after_alloc + 1 == free_before);

        expect_command_ok("Suite9-BlockFree", dr, "B-F 9 17 10");
        uint32_t free_after_free = get_free_sectors(fm, c.mount);
        REQUIRE(free_after_free == free_before);
    }

    print_scenario(suite, "Block commands on FAT fail");
    prepare_fat_partition(fm, dr, fat_path, 8, "FAT-BLOCK");
    expect_command_status_prefix("Suite9-FAT-Select", dr, "CP8", "02,PARTITION SELECTED");
    open_buffer_channel("Suite9-FAT-OpenBuffer", dr, 2);
    expect_command_status_prefix("Suite9-FAT-BlockRead", dr, "B-R 2 8 17 0", "78,BLOCK ACCESS DENIED");
    expect_command_status_prefix("Suite9-FAT-BlockWrite", dr, "B-W 2 8 17 0", "78,BLOCK ACCESS DENIED");
    expect_command_status_prefix("Suite9-FAT-BlockAllocate", dr, "B-A 8 17 0", "78,BLOCK ACCESS DENIED");
    expect_command_status_prefix("Suite9-FAT-BlockFree", dr, "B-F 8 17 0", "78,BLOCK ACCESS DENIED");
    close_file(dr, 2);

    printf("Suite9 completed successfully!\n");
}


// ---------------------------------------------------------------------------
// Suite10: the command channel as a Commodore actually drives it.
//
// The suites above build their command strings by hand. A Commodore does not.
// PRINT# appends a carriage return to every command, and BASIC prints a space
// before and after every number, so the bytes that arrive carry a terminator and
// separators that none of the earlier suites ever sent. Issues #875 and #876 both
// live in exactly that gap. Suite10 sends the byte sequences a Commodore produces
// and checks the command surface against the CMD and Commodore documentation.
// ---------------------------------------------------------------------------

#include "blockdev_file.h"
#include "filesystem_d64.h"

typedef enum { e_image_d64, e_image_d71, e_image_d81, e_image_dnp } image_kind_t;

static void create_formatted_image(FileManager *fm, const char *path, const char *diskname,
                                   int blocks, image_kind_t kind)
{
    const char *testname = "create_formatted_image";
    File *f = NULL;
    FRESULT fres = fm->fopen(path, FA_CREATE_ALWAYS | FA_WRITE | FA_READ, &f);
    if (fres != FR_OK) {
        printf("%s: could not create %s: %s\n", testname, path, FileSystem::get_error_string(fres));
    }
    REQUIRE(fres == FR_OK);

    uint8_t empty[256];
    memset(empty, 0, sizeof(empty));
    for (int i = 0; i < blocks; i++) {
        uint32_t transferred = 0;
        fres = f->write(empty, sizeof(empty), &transferred);
        REQUIRE(fres == FR_OK);
        REQUIRE(transferred == sizeof(empty));
    }

    {
        // The file system stack borrows the File, so it has to be gone before the
        // file is closed. This is what the REST image creation routes do.
        BlockDevice_File blk(f, 256);
        Partition prt(&blk, 0, 0, 0);
        switch (kind) {
        case e_image_d64: { FileSystemD64 fs(&prt, true); fres = fs.format(diskname); break; }
        case e_image_d71: { FileSystemD71 fs(&prt, true); fres = fs.format(diskname); break; }
        case e_image_d81: { FileSystemD81 fs(&prt, true); fres = fs.format(diskname); break; }
        default:          { FileSystemDNP fs(&prt, true); fres = fs.format(diskname); break; }
        }
    }
    fm->fclose(f);
    if (fres != FR_OK) {
        printf("%s: could not format %s: %s\n", testname, path, FileSystem::get_error_string(fres));
    }
    REQUIRE(fres == FR_OK);
}

static void expect_path_exists(const char *testname, FileManager *fm, const char *path)
{
    if (!fm->is_path_valid(path)) {
        printf("%s: expected '%s' to exist\n", testname, path);
    }
    REQUIRE(fm->is_path_valid(path));
}

static void expect_path_absent(const char *testname, FileManager *fm, const char *path)
{
    if (fm->is_path_valid(path)) {
        printf("%s: expected '%s' to be gone\n", testname, path);
    }
    REQUIRE(!fm->is_path_valid(path));
}

// PRINT#15,"P"+CHR$(96+channel)+CHR$(lo)+CHR$(hi)+CHR$(offset) is the documented
// form of the position command: the channel byte carries the secondary address plus
// 96, and BASIC appends a carriage return.
static void expect_rel_position_basic_form(const char *testname, IecDrive *dr, uint8_t chan,
                                           uint16_t record, uint8_t offset, const char *expected)
{
    uint8_t cmd[6] = {
        'P',
        (uint8_t)(96 + chan),
        (uint8_t)(record & 0xFF),
        (uint8_t)(record >> 8),
        offset,
        0x0D
    };
    send_command_data(dr, cmd, sizeof(cmd));
    get_status(dr);
    if (strcmp(last_status, expected) != 0) {
        printf("%s: P channel %u record %u offset %u: status was '%s', expected '%s'\n",
               testname, chan, record, offset, last_status, expected);
    }
    REQUIRE(strcmp(last_status, expected) == 0);
}

static void run_suite10_command_terminator(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite10";

    print_scenario("Suite10", "Commands carrying the carriage return that PRINT# appends");
    prepare_fat_partition(fm, dr, "/Fat/s10", 12, "CMDPART");
    FRESULT fres = fm->create_dir("/Fat/s10/os");
    REQUIRE(fres == FR_OK || fres == FR_EXIST);

    expect_command_response("Suite10-CP12", dr, "CP12\r", "02,PARTITION SELECTED,12,00\r");

    // PRINT#15,"CD//OS" is the reproduction in issue #875.
    expect_command_ok("Suite10-CD-FROM-ROOT", dr, "CD//OS\r");
    expect_command_response("Suite10-PWD-IN-SUBDIR", dr, "XPWD\r", "12:/OS/");
    expect_command_ok("Suite10-CD-TO-ROOT", dr, "CD//\r");
    expect_command_response("Suite10-PWD-AT-ROOT", dr, "XPWD\r", "12:/");
    expect_command_ok("Suite10-CD-BY-NAME", dr, "CD:OS\r");
    expect_command_response("Suite10-PWD-AFTER-NAME", dr, "XPWD\r", "12:/OS/");
    expect_command_ok("Suite10-CD-TO-PARENT", dr, "CD_\r");
    expect_command_response("Suite10-PWD-AFTER-PARENT", dr, "XPWD\r", "12:/");

    // A carriage return that reached the name would create a directory whose name
    // ends in one, which nothing could then open again.
    expect_command_ok("Suite10-MD", dr, "MD:MADEDIR\r");
    expect_path_exists("Suite10-MD-EXACT-NAME", fm, "/Fat/s10/MADEDIR");
    expect_directory_contains("Suite10-DIR-SHOWS-MADEDIR", dr, "$12", "\"MADEDIR\"");
    expect_command_ok("Suite10-CD-INTO-MADEDIR", dr, "CD:MADEDIR\r");
    expect_command_ok("Suite10-CD-OUT-OF-MADEDIR", dr, "CD//\r");
    expect_command_ok("Suite10-RD", dr, "RD:MADEDIR\r");
    expect_path_absent("Suite10-RD-REMOVED", fm, "/Fat/s10/MADEDIR");

    // The position command carries four binary parameters. With the carriage return
    // counted as one of them, a five byte command became a six byte one and the
    // record number and the offset were read as a plain byte position instead.
    const uint8_t chan = 4;
    const uint8_t record_size = 16;
    uint8_t first[16], second[16];
    memset(first, 'A', sizeof(first));
    memset(second, 'B', sizeof(second));

    expect_rel_open("Suite10-RelCreate", dr, chan, "12:RELPOS", record_size);
    expect_rel_position_status("Suite10-RelSeekNew1", dr, chan, 1, 1, "50,RECORD NOT PRESENT,00,00\r");
    expect_rel_write("Suite10-RelWrite1", dr, chan, first, sizeof(first));
    expect_rel_position_status("Suite10-RelSeekNew2", dr, chan, 2, 1, "50,RECORD NOT PRESENT,00,00\r");
    expect_rel_write("Suite10-RelWrite2", dr, chan, second, sizeof(second));
    expect_rel_position_basic_form("Suite10-RelSeekBasicForm", dr, chan, 1, 1, "00, OK,00,00\r");
    expect_rel_read("Suite10-RelReadFirst", dr, chan, first, sizeof(first));
    expect_rel_read("Suite10-RelReadSecond", dr, chan, second, sizeof(second));
    close_file(dr, chan);
    expect_status_ok("Suite10-RelClose", "12:RELPOS");

    // A 64 byte command, which filled the buffer before it held 254 bytes, is executed
    // with its 64th byte: the partition number ends there. A command that fills the
    // buffer now is refused (SI-022), which Suite11-SI022-TooLong checks.
    char full[65];
    memset(full, ' ', 64);
    memcpy(full, "CP", 2);
    memcpy(full + 62, "12", 2);
    full[64] = 0;
    expect_command_response("Suite10-FullBuffer", dr, full, "02,PARTITION SELECTED,12,00\r");

    // A command that is nothing but a carriage return carries no command at all, and
    // has to leave the command channel usable.
    send_command(dr, "\r");
    expect_command_response("Suite10-AFTER-EMPTY-COMMAND", dr, "CP12\r", "02,PARTITION SELECTED,12,00\r");

    // C<shift-P> takes its partition number as a byte, and that byte can be the same
    // carriage return BASIC appends. Partition 13 is the case, and it is what the
    // JiffyDOS command @"C<shift-P>"+CHR$(13) sends: three bytes and no terminator.
    prepare_fat_partition(fm, dr, "/Fat/s10_p13", 13, "PART13");
    const uint8_t cp13[3] = { 'C', 0xD0, 0x0D };
    expect_command_data_response("Suite10-CP-BINARY-13", dr, cp13, sizeof(cp13),
                                 "02,PARTITION SELECTED,13,00\r");
    // The same command from BASIC, where PRINT# adds the terminator behind it.
    const uint8_t cp13_term[4] = { 'C', 0xD0, 0x0D, 0x0D };
    expect_command_data_response("Suite10-CP-BINARY-13-TERMINATED", dr, cp13_term, sizeof(cp13_term),
                                 "02,PARTITION SELECTED,13,00\r");
    const uint8_t cp12[3] = { 'C', 0xD0, 12 };
    expect_command_data_response("Suite10-CP-BINARY-12", dr, cp12, sizeof(cp12),
                                 "02,PARTITION SELECTED,12,00\r");
    const uint8_t cp_missing[2] = { 'C', 0xD0 };
    expect_command_data_response("Suite10-CP-BINARY-NO-PARAMETER", dr, cp_missing, sizeof(cp_missing),
                                 "30,SYNTAX ERROR,00,00\r");
}

// SI-040, SI-061, SI-062, SI-066: CP, CD in every documented form, CD into an image,
// and XPWD.
static void run_suite10_directory_navigation(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite10";
    FRESULT fres;

    print_scenario("Suite10", "CD, MD, RD and CP in the forms the CMD manual documents");
    prepare_fat_partition(fm, dr, "/Fat/nav", 30, "NAVIGATE");
    fres = fm->create_dir("/Fat/nav/sub");
    REQUIRE(fres == FR_OK || fres == FR_EXIST);
    fres = fm->create_dir("/Fat/nav/sub/deep");
    REQUIRE(fres == FR_OK || fres == FR_EXIST);
    create_formatted_image(fm, "/Fat/nav/img.d64", "NAVIMAGE", 683, e_image_d64);

    expect_command_response("Suite10-CP30", dr, "CP30\r", "02,PARTITION SELECTED,30,00\r");

    // A path that begins with two slashes starts at the partition root, one slash or
    // a colon introduces a name relative to the current directory, and each further
    // subdirectory is separated by a single slash.
    expect_command_ok("Suite10-NavRoot", dr, "CD//\r");
    expect_command_ok("Suite10-NavDeepFromRoot", dr, "CD//SUB/DEEP\r");
    expect_command_response("Suite10-NavPwdDeep", dr, "XPWD\r", "30:/SUB/DEEP/");
    expect_command_ok("Suite10-NavRootAgain", dr, "CD//\r");
    expect_command_ok("Suite10-NavColonAfterPath", dr, "CD//SUB/:DEEP\r");
    expect_command_response("Suite10-NavPwdColonForm", dr, "XPWD\r", "30:/SUB/DEEP/");
    expect_command_ok("Suite10-NavTrailingSlash", dr, "CD//SUB/DEEP/\r");
    expect_command_response("Suite10-NavPwdTrailingSlash", dr, "XPWD\r", "30:/SUB/DEEP/");

    // The left arrow, which arrives as an underscore, moves to the parent both when
    // it follows the command word and when it stands behind a colon.
    expect_command_ok("Suite10-NavParentBare", dr, "CD_\r");
    expect_command_response("Suite10-NavPwdParentBare", dr, "XPWD\r", "30:/SUB/");
    expect_command_ok("Suite10-NavParentColon", dr, "CD:_\r");
    expect_command_response("Suite10-NavPwdParentColon", dr, "XPWD\r", "30:/");

    // A name behind the colon is one component, as it is on the CMD devices: the slash
    // in it is a character of the name and not a path separator, so this enters
    // nothing (SI-011a).
    expect_command_ok("Suite10-NavRootBeforeColonPath", dr, "CD//\r");
    expect_command_response("Suite10-NavColonPathComponents", dr, "CD:SUB/DEEP\r",
                            "71,DIRECTORY ERROR,30,00\r");
    expect_command_response("Suite10-NavPwdAfterColonPath", dr, "XPWD\r", "30:/");

    // A partition number in front of the path selects the partition to act on.
    expect_command_ok("Suite10-NavPartitionPrefixed", dr, "CD30//SUB\r");
    expect_command_response("Suite10-NavPwdPartitionPrefixed", dr, "XPWD\r", "30:/SUB/");
    expect_command_ok("Suite10-NavBackToRoot", dr, "CD//\r");

    // A directory that is not there leaves the current directory where it was.
    expect_command_response("Suite10-NavMissing", dr, "CD//NOSUCH\r", "71,DIRECTORY ERROR,30,00\r");
    expect_command_response("Suite10-NavPwdAfterMissing", dr, "XPWD\r", "30:/");

    // A mounted disk image is entered and left like a directory.
    expect_command_ok("Suite10-NavEnterImage", dr, "CD:IMG.D64\r");
    expect_command_response("Suite10-NavPwdInImage", dr, "XPWD\r", "30:/IMG.D64/");
    // The header of a CBM image carries its volume name, padded to sixteen
    // characters, so only the opening quote and the name are matched here.
    expect_directory_contains("Suite10-NavImageHeader", dr, "$30", "\"NAVIMAGE ");
    expect_command_ok("Suite10-NavLeaveImage", dr, "CD:_\r");
    expect_command_response("Suite10-NavPwdAfterImage", dr, "XPWD\r", "30:/");

    // MD creates the name behind the colon relative to the path in front of it, and
    // RD removes a directory only when it is empty.
    expect_command_ok("Suite10-MdPlain", dr, "MD:MD1\r");
    expect_command_ok("Suite10-MdNested", dr, "MD/MD1/:MD2\r");
    expect_command_ok("Suite10-MdNestedFromRoot", dr, "MD//MD1/:MD3\r");
    expect_path_exists("Suite10-MdNestedExists", fm, "/Fat/nav/MD1/MD2");
    expect_path_exists("Suite10-MdNestedFromRootExists", fm, "/Fat/nav/MD1/MD3");
    expect_command_response("Suite10-RdNotEmpty", dr, "RD:MD1\r", "63,FILE EXISTS,00,00\r");
    // RD takes no path (SI-063): the parent is entered first.
    expect_command_ok("Suite10-RdEnterParent", dr, "CD//MD1\r");
    expect_command_ok("Suite10-RdNested", dr, "RD:MD2\r");
    expect_command_ok("Suite10-RdNestedFromRoot", dr, "RD:MD3\r");
    expect_command_ok("Suite10-RdLeaveParent", dr, "CD//\r");
    expect_command_ok("Suite10-RdNowEmpty", dr, "RD:MD1\r");
    expect_path_absent("Suite10-RdRemoved", fm, "/Fat/nav/MD1");
    expect_command_response("Suite10-RdMissing", dr, "RD:NOSUCH\r", "62,FILE NOT FOUND,00,00\r");

    // CP reports the partition it selected, CP without a number reports the current
    // one, and a partition that does not exist is refused.
    expect_command_response("Suite10-CpCurrent", dr, "CP\r", "02,PARTITION SELECTED,30,00\r");
    expect_command_response("Suite10-CpExplicit", dr, "CP12\r", "02,PARTITION SELECTED,12,00\r");
    expect_command_response("Suite10-CpSpaced", dr, "CP 30\r", "02,PARTITION SELECTED,30,00\r");
    expect_command_response("Suite10-CpIllegal", dr, "CP99\r", "77,SELECTED PARTITION ILLEGAL,99,00\r");
    expect_command_response("Suite10-CpBinary", dr, "C\xD0\x1E", "02,PARTITION SELECTED,30,00\r");
}

static void run_suite10_block_commands(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite10";
    const uint8_t chan = 2;
    const char *image = "/Fat/s10_blocks.d64";

    print_scenario("Suite10", "Block commands in the forms PRINT# produces");
    create_formatted_image(fm, image, "BLOCKTEST", 683, e_image_d64);
    dr->add_partition(13, image, "BLOCKPART");
    expect_command_response("Suite10-CP13", dr, "CP13\r", "02,PARTITION SELECTED,13,00\r");

    // PRINT#15,"U1:";2;0;18;0 sends "U1: 2  0  18  0 " and a carriage return, and
    // drive number 0 means the currently selected partition. This is the earlier of
    // the two "VIEW BAM" programs on the 1541 TEST/DEMO disk, and the reproduction in
    // issue #876. The 9/84 revision of the same program sends "U1:2,0,18,0" instead.
    open_buffer_channel("Suite10-OpenBuffer", dr, chan);
    expect_command_ok("Suite10-U1-SpaceForm", dr, "U1: 2  0  18  0 \r");

    uint8_t bam[256];
    read_buffer_channel("Suite10-ReadBam", dr, chan, bam, sizeof(bam));
    // Track 18 sector 0 of a 1541 disk links to the first directory sector and holds
    // the DOS version byte 'A'. All 256 bytes have to be readable after U1.
    if ((bam[0] != 18) || (bam[1] != 1) || (bam[2] != 'A')) {
        printf("%s: BAM sector did not start with 18/1/'A': %02x %02x %02x\n",
               testname, bam[0], bam[1], bam[2]);
        dump_hex_relative(bam, 16);
    }
    REQUIRE(bam[0] == 18);
    REQUIRE(bam[1] == 1);
    REQUIRE(bam[2] == 'A');

    // The separator spellings themselves are checked exhaustively, and against the
    // values they parse to, by the parser tests in target/pc/linux/parse. What is
    // checked here is that the whole drive reads the same sector for the form the
    // 9/84 VIEW BAM sends, for the UA spelling of U1, and for an explicit partition
    // number in place of drive 0.
    uint8_t again[256];
    static const char *read_forms[] = {
        "U1:2,0,18,0\r",
        "UA: 2  0  18  0 \r",
        "U1:2,13,18,0\r",
    };
    for (size_t i = 0; i < sizeof(read_forms) / sizeof(read_forms[0]); i++) {
        expect_command_ok("Suite10-BlockReadForm", dr, read_forms[i]);
        read_buffer_channel("Suite10-BlockReadForm", dr, chan, again, sizeof(again));
        if (memcmp(again, bam, sizeof(bam)) != 0) {
            printf("%s: '%s' did not read the same sector\n", testname, read_forms[i]);
        }
        REQUIRE(memcmp(again, bam, sizeof(bam)) == 0);
    }

    // B-R is accepted in the same spellings. What it leaves the buffer pointer at is
    // deliberately not asserted here: on a real drive B-R sets it from the first byte
    // of the sector while U1 exposes all 256 bytes, and this drive does not make that
    // distinction. That difference is a separate question from the one #876 reports.
    expect_command_ok("Suite10-BlockReadBR", dr, "B-R:2,0,18,0\r");

    // PRINT#15,"B-P:";2;144 sends "B-P: 2  144 ", which is how VIEW BAM reaches the
    // disk name inside the sector it just read.
    expect_command_ok("Suite10-BP-SpaceForm", dr, "U1: 2  0  18  0 \r");
    read_buffer_channel("Suite10-BP-Prefill", dr, chan, again, sizeof(again));
    expect_command_ok("Suite10-BP-Position", dr, "B-P: 2  144 \r");
    uint8_t name_area[16];
    read_buffer_channel("Suite10-BP-ReadName", dr, chan, name_area, sizeof(name_area));
    if (memcmp(name_area, bam + 144, sizeof(name_area)) != 0) {
        printf("%s: the buffer pointer did not move to offset 144\n", testname);
        dump_hex_relative(name_area, sizeof(name_area));
    }
    REQUIRE(memcmp(name_area, bam + 144, sizeof(name_area)) == 0);
    expect_command_ok("Suite10-BP-CommaForm", dr, "B-P:2,144\r");
    read_buffer_channel("Suite10-BP-ReadNameAgain", dr, chan, name_area, sizeof(name_area));
    REQUIRE(memcmp(name_area, bam + 144, sizeof(name_area)) == 0);

    // CBM DOS keeps only the low byte of a buffer position, so 300 is 44 and not a
    // refusal.
    expect_command_ok("Suite10-BP-PastEnd", dr, "B-P:2,300\r");
    read_buffer_channel("Suite10-BP-ReadWrapped", dr, chan, name_area, sizeof(name_area));
    REQUIRE(memcmp(name_area, bam + 44, sizeof(name_area)) == 0);

    // A block written in the same form, read back through U1 and through UB.
    uint8_t payload[256];
    make_increment_pattern(payload);
    expect_command_ok("Suite10-BP-Rewind", dr, "B-P: 2  0 \r");
    send_channel_data(dr, chan, payload, sizeof(payload));
    expect_command_ok("Suite10-U2-SpaceForm", dr, "U2: 2  0  17  10 \r");
    expect_command_ok("Suite10-U1-ReadBack", dr, "U1: 2  0  17  10 \r");
    uint8_t written[256];
    read_buffer_channel("Suite10-ReadBackWritten", dr, chan, written, sizeof(written));
    REQUIRE(memcmp(written, payload, sizeof(payload)) == 0);

    make_sector_payload(payload, "UB WRITE");
    expect_command_ok("Suite10-BP-RewindAgain", dr, "B-P: 2  0 \r");
    send_channel_data(dr, chan, payload, sizeof(payload));
    expect_command_ok("Suite10-UB-Alias", dr, "UB:2,0,17,10\r");
    expect_command_ok("Suite10-U1-ReadBackUB", dr, "U1:2,0,17,10\r");
    read_buffer_channel("Suite10-ReadBackUB", dr, chan, written, sizeof(written));
    REQUIRE(memcmp(written, payload, sizeof(payload)) == 0);

    close_file(dr, chan);
    expect_status_ok("Suite10-CloseBuffer", "#2");

    // Allocating and freeing in the same form. Sector 17/11 is free on a disk that
    // was just formatted.
    // The manuals write these with three numbers: the drive or partition, the track
    // and the sector. There is no channel, because neither command touches a buffer.
    uint32_t free_before = get_free_sectors(fm, image);
    expect_command_ok("Suite10-BA-SpaceForm", dr, "B-A: 0  17  11 \r");
    REQUIRE(get_free_sectors(fm, image) + 1 == free_before);
    expect_command_ok("Suite10-BF-CommaForm", dr, "B-F:0,17,11\r");
    REQUIRE(get_free_sectors(fm, image) == free_before);

    // Refusals. Too few parameters is a syntax error whatever the separators are, a
    // track or sector outside the disk is refused, and a partition that is a plain
    // directory has no sectors to address at all.
    open_buffer_channel("Suite10-ReopenBuffer", dr, chan);
    expect_command_status_prefix("Suite10-U1-TooFew", dr, "U1: 2  0 \r", "30,SYNTAX ERROR");
    expect_command_status_prefix("Suite10-BR-NoParams", dr, "B-R:\r", "30,SYNTAX ERROR");
    expect_command_status_prefix("Suite10-BR-NotNumeric", dr, "B-R:X,0,18,0\r", "30,SYNTAX ERROR");
    expect_command_status_prefix("Suite10-U1-BadTrack", dr, "U1:2,0,99,0\r", "66,ILLEGAL TRACK OR SECTOR");
    expect_command_status_prefix("Suite10-U1-BadSector", dr, "U1:2,0,18,99\r", "66,ILLEGAL TRACK OR SECTOR");
    close_file(dr, chan);
    // A channel opened while a directory partition is current has no sectors to address;
    // the partition number in the command does not change which partition it uses.
    expect_command_response("Suite10-CP12", dr, "CP12\r", "02,PARTITION SELECTED,12,00\r");
    open_buffer_channel("Suite10-OpenBufferOnDirectory", dr, chan);
    expect_command_status_prefix("Suite10-U1-OnDirectory", dr, "U1:2,13,18,0\r", "78,BLOCK ACCESS DENIED");
    close_file(dr, chan);
    // A block command on a channel that is not a direct access channel names no buffer.
    expect_command_status_prefix("Suite10-U1-NoBuffer", dr, "U1:2,0,18,0\r", "70,NO CHANNEL");
    expect_command_response("Suite10-CP13-Again", dr, "CP13\r", "02,PARTITION SELECTED,13,00\r");
}

static void run_suite10_user_commands(IecDrive *dr)
{
    const char *testname = "Suite10";

    print_scenario("Suite10", "User commands and the error channel");

    // CBM DOS takes the user command from the low four bits of the character after
    // the U, so U9 and UI are one command and U: and UJ are another. Both report the
    // DOS version, which is how a drive answers after it has been initialised.
    expect_command_status_prefix("Suite10-UI", dr, "UI\r", "73,");
    expect_command_status_prefix("Suite10-U9", dr, "U9\r", "73,");
    expect_command_status_prefix("Suite10-UJ", dr, "UJ\r", "73,");
    expect_command_status_prefix("Suite10-UColon", dr, "U:\r", "73,");
    // I is not UI: it initialises and answers OK (SI-053).
    expect_command_status_prefix("Suite10-I", dr, "I0\r", "00, OK");

    // U3 to U8 jump into a drive buffer, which this drive has no equivalent for. U is
    // a command letter, so that is 30 (SI-104); Z is not, so that is 31 (SI-031).
    expect_command_status_prefix("Suite10-U3", dr, "U3:2,0,18,0\r", "30,SYNTAX ERROR");
    expect_command_status_prefix("Suite10-Unknown", dr, "ZZ\r", "31,SYNTAX ERROR");

    // Reading the error channel clears it, as it does on a real drive.
    expect_command_status_prefix("Suite10-ErrorSet", dr, "CD//NOSUCH\r", "71,DIRECTORY ERROR");
    get_status(dr);
    expect_current_status("Suite10-ErrorCleared", "status re-read", "00, OK,00,00\r");
}

// A directory line is 32 bytes long when no time stamp was asked for: two link
// bytes, the block count, the name padded and in quotes, and the three character
// type. In a partition directory the block count is the partition number.
static void expect_partition_line(const char *testname, const uint8_t *listing, int length,
                                  int part, const char *name, const char *type)
{
    for (int offset = 32; offset + 32 <= length; offset += 32) {
        const uint8_t *line = listing + offset;
        int blocks = line[2] | (line[3] << 8);
        if (blocks != part) {
            continue;
        }
        int digits = 1;
        for (int n = blocks; n >= 10; n /= 10) {
            digits++;
        }
        char shown[24]; // a quoted name is at most sixteen characters and two quotes
        int len = snprintf(shown, sizeof(shown), "\"%s\"", name);
        REQUIRE(len < (int)sizeof(shown));
        int quote_at = 4 + (3 - digits) + 1;
        int type_at = 27 - digits;
        bool name_ok = (memcmp(line + quote_at, shown, len) == 0);
        bool type_ok = (memcmp(line + type_at, type, 3) == 0);
        if (!name_ok || !type_ok) {
            char got_type[4] = { 0 };
            memcpy(got_type, line + type_at, 3);
            printf("%s: partition %d has type '%s', expected name %s type '%s'\n",
                   testname, part, got_type, shown, type);
            dump_hex_relative(line, 32);
        }
        REQUIRE(name_ok);
        REQUIRE(type_ok);
        return;
    }
    printf("%s: partition %d does not appear in the partition directory\n", testname, part);
    dump_hex_relative(listing, length);
    REQUIRE(false);
}

// The other half of the check above: a partition the filter was meant to leave out.
static void expect_partition_absent(const char *testname, const uint8_t *listing, int length, int part)
{
    for (int offset = 32; offset + 32 <= length; offset += 32) {
        int blocks = listing[offset + 2] | (listing[offset + 3] << 8);
        if (blocks == part) {
            printf("%s: partition %d is in the listing and should not be\n", testname, part);
            dump_hex_relative(listing + offset, 32);
        }
        REQUIRE(blocks != part);
    }
}

// Reads a whole directory stream, whether of files or of partitions.
static int read_directory_stream(const char *testname, IecDrive *dr, const char *name,
                                    uint8_t *listing, int size)
{
    memset(listing, 0, size);
    open_file(dr, 0, name);
    get_status(dr);
    expect_status_ok(testname, name);
    int got = read_file(dr, 0, listing, size);
    REQUIRE(got > 0);
    close_file(dr, 0);
    expect_status_ok(testname, name);
    return got;
}

// SI-043, SI-044, SI-047, SI-048, SI-049, SI-050: the partition directory, its type
// column, its block count column, its header name and the absence of a SYSTEM line.
static void run_suite10_partition_directory(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite10";

    print_scenario("Suite10", "Partition directory names and types");
    prepare_fat_partition(fm, dr, "/Fat/s10_native", 20, "NATIVE DIR");
    create_formatted_image(fm, "/Fat/s10_p.d64", "P41", 683, e_image_d64);
    create_formatted_image(fm, "/Fat/s10_p.d71", "P71", 1366, e_image_d71);
    create_formatted_image(fm, "/Fat/s10_p.d81", "P81", 3200, e_image_d81);
    create_formatted_image(fm, "/Fat/s10_p.dnp", "PNAT", 4 * 256, e_image_dnp);
    dr->add_partition(21, "/Fat/s10_p.d64", "IMAGE 1541");
    dr->add_partition(22, "/Fat/s10_p.d71", "IMAGE 1571");
    dr->add_partition(23, "/Fat/s10_p.d81", "IMAGE 1581");
    dr->add_partition(24, "/Fat/s10_p.dnp", "IMAGE NATIVE");

    uint8_t listing[8192];
    int got = read_directory_stream(testname, dr, "$=P", listing, sizeof(listing));

    // The name is the partition name, not the path it is rooted at, and the type
    // follows the file system at that root: CMD DOS reports a native partition as
    // NAT and a drive emulation partition by the model it emulates. This is #877.
    expect_partition_line("Suite10-PartNativeDir", listing, got, 20, "NATIVE DIR", "NAT");
    expect_partition_line("Suite10-PartD64", listing, got, 21, "IMAGE 1541", "41 ");
    expect_partition_line("Suite10-PartD71", listing, got, 22, "IMAGE 1571", "71 ");
    expect_partition_line("Suite10-PartD81", listing, got, 23, "IMAGE 1581", "81 ");
    expect_partition_line("Suite10-PartDnp", listing, got, 24, "IMAGE NATIVE", "NAT");

    // The partition directory takes a pattern, like any other directory.
    got = read_directory_stream(testname, dr, "$=P:IMAGE 15?1", listing, sizeof(listing));
    expect_partition_line("Suite10-PartFilteredD64", listing, got, 21, "IMAGE 1541", "41 ");
    expect_partition_line("Suite10-PartFilteredD81", listing, got, 23, "IMAGE 1581", "81 ");
    expect_partition_absent("Suite10-PartPatternExcludes", listing, got, 20);

    // It also takes a type, which is what the second half of LOAD"$=P:*=tp" selects.
    got = read_directory_stream(testname, dr, "$=P:*=4", listing, sizeof(listing));
    expect_partition_line("Suite10-PartTypeD64", listing, got, 21, "IMAGE 1541", "41 ");
    expect_partition_absent("Suite10-PartTypeExcludesNative", listing, got, 20);
    expect_partition_absent("Suite10-PartTypeExcludesD71", listing, got, 22);

    got = read_directory_stream(testname, dr, "$=P:*=N", listing, sizeof(listing));
    expect_partition_line("Suite10-PartTypeNativeDir", listing, got, 20, "NATIVE DIR", "NAT");
    expect_partition_line("Suite10-PartTypeNativeDnp", listing, got, 24, "IMAGE NATIVE", "NAT");
    expect_partition_absent("Suite10-PartTypeNativeExcludesD64", listing, got, 21);

    // Several types at once, and one this drive can never have.
    got = read_directory_stream(testname, dr, "$=P:*=4,8", listing, sizeof(listing));
    expect_partition_line("Suite10-PartTypesD64", listing, got, 21, "IMAGE 1541", "41 ");
    expect_partition_line("Suite10-PartTypesD81", listing, got, 23, "IMAGE 1581", "81 ");
    expect_partition_absent("Suite10-PartTypesExcludeD71", listing, got, 22);

    got = read_directory_stream(testname, dr, "$=P:*=C", listing, sizeof(listing));
    for (int part = 20; part <= 24; part++) {
        expect_partition_absent("Suite10-PartTypeCpm", listing, got, part);
    }
}

// The reply to G-P: thirty bytes and a carriage return. Byte 0 is the CMD partition
// type, byte 1 is reserved, byte 2 is the partition number and bytes 3 to 18 carry
// the name the partition directory shows.
static void expect_partition_info(const char *testname, IecDrive *dr, const uint8_t *cmd,
                                  int len, int type, int part, const char *name)
{
    send_command_data(dr, cmd, len);
    get_status(dr);
    bool ok = (last_status_size == 31) && (last_status[0] == type) && (last_status[1] == 0) &&
              (last_status[2] == part) && (last_status[30] == 0x0D) &&
              (strncmp(last_status + 3, name, 16) == 0);
    if (!ok) {
        char got_name[17] = { 0 };
        memcpy(got_name, last_status + 3, 16);
        printf("%s: %d bytes, type %d, partition %d, name '%s'; expected 31 bytes, type %d, "
               "partition %d, name '%s'\n", testname, last_status_size, last_status[0],
               last_status[2], got_name, type, part, name);
        dump_hex_relative((uint8_t *)last_status, last_status_size);
    }
    REQUIRE(ok);
}

static void run_suite10_partition_info(FileManager *fm, IecDrive *dr)
{
    print_scenario("Suite10", "Partition information");
    // Partition 13 is the one whose number is the same byte as the terminator, and
    // an earlier scenario has since mounted an image there.
    prepare_fat_partition(fm, dr, "/Fat/s10_p13", 13, "PART13");

    // The partitions the directory scenario above created, read back one at a time.
    // The type is the CMD code the directory prints as NAT, 41, 71 and 81.
    const uint8_t native_dir[4] = { 'G', '-', 'P', 20 };
    expect_partition_info("Suite10-InfoNativeDir", dr, native_dir, sizeof(native_dir), 1, 20, "NATIVE DIR");
    const uint8_t d64[4] = { 'G', '-', 'P', 21 };
    expect_partition_info("Suite10-InfoD64", dr, d64, sizeof(d64), 2, 21, "IMAGE 1541");
    const uint8_t d71[4] = { 'G', '-', 'P', 22 };
    expect_partition_info("Suite10-InfoD71", dr, d71, sizeof(d71), 3, 22, "IMAGE 1571");
    const uint8_t d81[4] = { 'G', '-', 'P', 23 };
    expect_partition_info("Suite10-InfoD81", dr, d81, sizeof(d81), 4, 23, "IMAGE 1581");
    const uint8_t dnp[4] = { 'G', '-', 'P', 24 };
    expect_partition_info("Suite10-InfoDnp", dr, dnp, sizeof(dnp), 1, 24, "IMAGE NATIVE");

    // With the terminator BASIC appends, and with a number that is not a partition.
    const uint8_t d64_term[5] = { 'G', '-', 'P', 21, 0x0D };
    expect_partition_info("Suite10-InfoTerminated", dr, d64_term, sizeof(d64_term), 2, 21, "IMAGE 1541");
    // A partition that does not exist reports type 0, which CMD DOS calls "not
    // created". Asking about one is not an error.
    const uint8_t missing[4] = { 'G', '-', 'P', 99 };
    expect_partition_info("Suite10-InfoAbsent", dr, missing, sizeof(missing), 0, 99, "");

    // No number and 255 both ask about the partition already selected.
    expect_command_response("Suite10-InfoSelect", dr, "CP22\r", "02,PARTITION SELECTED,22,00\r");
    const uint8_t current[3] = { 'G', '-', 'P' };
    expect_partition_info("Suite10-InfoCurrent", dr, current, sizeof(current), 3, 22, "IMAGE 1571");
    const uint8_t current255[4] = { 'G', '-', 'P', 255 };
    expect_partition_info("Suite10-InfoCurrent255", dr, current255, sizeof(current255), 3, 22, "IMAGE 1571");
    // Partition 13 is the number whose byte is the terminator. The CMD manual asks
    // for the terminator to be sent as well, and with it the partition is reached;
    // without it the command reads as though it carried no number at all.
    const uint8_t part13[5] = { 'G', '-', 'P', 0x0D, 0x0D };
    expect_partition_info("Suite10-InfoPartition13", dr, part13, sizeof(part13), 1, 13, "PART13");
    const uint8_t part13_bare[4] = { 'G', '-', 'P', 0x0D };
    expect_partition_info("Suite10-InfoPartition13Bare", dr, part13_bare, sizeof(part13_bare), 3, 22, "IMAGE 1571");
}

static void run_suite10_directory_streams(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite10";
    uint32_t tr;

    print_scenario("Suite10", "Directory streams and their filters");
    prepare_fat_partition(fm, dr, "/Fat/list", 31, "LISTING");
    fm->save_file(true, "/Fat/list", "one.prg", (const uint8_t *)"ONE", 3, &tr);
    fm->save_file(true, "/Fat/list", "two.seq", (const uint8_t *)"TWO", 3, &tr);
    FRESULT fres = fm->create_dir("/Fat/list/three");
    REQUIRE(fres == FR_OK || fres == FR_EXIST);
    expect_command_response("Suite10-CP31", dr, "CP31\r", "02,PARTITION SELECTED,31,00\r");

    // The header of a directory carries the partition name, and the entries carry
    // the CBM file type that the stored extension maps to.
    expect_directory_contains("Suite10-ListHeader", dr, "$31", "\"LISTING ");
    expect_directory_contains("Suite10-ListPrg", dr, "$31", "\"ONE\"");
    expect_directory_contains("Suite10-ListSeq", dr, "$31", "\"TWO\"");
    expect_directory_contains("Suite10-ListDir", dr, "$31", "\"THREE\"");
    expect_directory_contains("Suite10-ListBlocksFree", dr, "$31", "BLOCKS FREE.");

    // A type filter keeps only the matching entries, and a pattern keeps only the
    // matching names.
    uint8_t listing[4096];
    int got = read_directory_stream(testname, dr, "$31:*=P", listing, sizeof(listing));
    REQUIRE(memmem(listing, got, "\"ONE\"", 5) != NULL);
    REQUIRE(memmem(listing, got, "\"TWO\"", 5) == NULL);
    REQUIRE(memmem(listing, got, "\"THREE\"", 7) == NULL);

    got = read_directory_stream(testname, dr, "$31:*=S", listing, sizeof(listing));
    REQUIRE(memmem(listing, got, "\"TWO\"", 5) != NULL);
    REQUIRE(memmem(listing, got, "\"ONE\"", 5) == NULL);

    got = read_directory_stream(testname, dr, "$31:*=D", listing, sizeof(listing));
    REQUIRE(memmem(listing, got, "\"THREE\"", 7) != NULL);
    REQUIRE(memmem(listing, got, "\"ONE\"", 5) == NULL);

    got = read_directory_stream(testname, dr, "$31:O*", listing, sizeof(listing));
    REQUIRE(memmem(listing, got, "\"ONE\"", 5) != NULL);
    REQUIRE(memmem(listing, got, "\"TWO\"", 5) == NULL);

    // A directory of another partition is read without leaving this one.
    expect_directory_contains("Suite10-ListOtherPartition", dr, "$12", "\"CMDPART ");
    expect_command_response("Suite10-StillOnPartition31", dr, "XPWD\r", "31:/");
}

void execute_suite10(FileManager *fm, IecDrive *dr)
{
    run_suite10_command_terminator(fm, dr);
    run_suite10_directory_navigation(fm, dr);
    run_suite10_block_commands(fm, dr);
    run_suite10_user_commands(dr);
    run_suite10_partition_directory(fm, dr);
    run_suite10_partition_info(fm, dr);
    run_suite10_directory_streams(fm, dr);

    printf("Suite10 completed successfully!\n");
}

// ---------------------------------------------------------------------------
// Suite11: doc/softiec_compatibility_spec.md, one case per requirement, each named
// after the paragraph it checks.
//
// Every case works on a partition of its own and sets it up itself, so a case can be
// run alone: `./result/testdrive SI031` runs only the Suite11 cases whose name
// contains SI031 and skips every other suite. That is how a requirement is shown to
// fail on its own before its change and to pass after it.
// ---------------------------------------------------------------------------

#include "iec_channel.h"
#include "iec_log.h"

// A fresh directory on the FAT file, mounted as partition 40, selected, and entered at
// its root.
static const char *s11_partition(FileManager *fm, IecDrive *dr, const char *dir)
{
    const char *testname = "Suite11";
    static char path[64];
    snprintf(path, sizeof(path), "/Fat/s11_%s", dir);
    FRESULT fres = fm->create_dir(path);
    REQUIRE(fres == FR_OK || fres == FR_EXIST);
    dr->add_partition(40, path, "SUITE11");
    expect_command_response(testname, dr, "CP40\r", "02,PARTITION SELECTED,40,00\r");
    expect_command_ok(testname, dr, "CD//\r");
    return path;
}

// SI-031: a command whose first byte is not a command letter answers 31, which is
// what the 1541 ROM loads at $C175 when its command table has no match.
static void s11_si031_unrecognised(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI031-Unrecognised";
    s11_partition(fm, dr, "si031");
    // CHR$(0) is the command C64 OS sends during its boot, measured as 33 on #877.
    const uint8_t chr0[1] = { 0 };
    expect_command_data_response(testname, dr, chr0, 1, "31,SYNTAX ERROR,00,00\r");
    expect_command_response(testname, dr, "Z\r", "31,SYNTAX ERROR,00,00\r");
    expect_command_response(testname, dr, "Q", "31,SYNTAX ERROR,00,00\r");
}

// SI-030: a command letter followed by a sub-command that does not exist answers 30,
// because the command was recognised. U3 jumps into drive memory (SI-104) and B-E
// executes a drive buffer (SI-095); neither exists here.
static void s11_si030_unknown_subcommand(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI030-UnknownSubcommand";
    s11_partition(fm, dr, "si030a");
    expect_command_response(testname, dr, "U3:2,0,18,0\r", "30,SYNTAX ERROR,00,00\r");
    expect_command_response(testname, dr, "B-E:2,0,18,0\r", "30,SYNTAX ERROR,00,00\r");
}

// SI-030: a colon with nothing after it is a missing name, 34.
static void s11_si030_missing_name(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI030-MissingName";
    s11_partition(fm, dr, "si030b");
    expect_command_response(testname, dr, "C:NEW=\r", "34,SYNTAX ERROR,00,00\r");
    expect_command_response(testname, dr, "R:NEW=\r", "34,SYNTAX ERROR,00,00\r");
}

// SI-030: a wildcard in the target of a copy or a rename is an illegal name, 33.
static void s11_si030_wildcard_target(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI030-WildcardTarget";
    s11_partition(fm, dr, "si030c");
    expect_command_response(testname, dr, "C:NEW*=OLD\r", "33,SYNTAX ERROR,00,00\r");
    expect_command_response(testname, dr, "R:NEW?=OLD\r", "33,SYNTAX ERROR,00,00\r");
}

// SI-036: a block command outside the disk is error 66, which CBM DOS names, and not
// the Ultimate's own 69 with a file system result code in the track field.
static void s11_si036_block_range(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI036-BlockRange";
    const char *image = "/Fat/s11_si036.d64";
    create_formatted_image(fm, image, "RANGE", 683, e_image_d64);
    dr->add_partition(41, image, "RANGE");
    expect_command_response(testname, dr, "CP41\r", "02,PARTITION SELECTED,41,00\r");
    open_buffer_channel(testname, dr, 2);
    expect_command_response(testname, dr, "U1:2,0,99,0\r", "66,ILLEGAL TRACK OR SECTOR,99,00\r");
    expect_command_response(testname, dr, "U1:2,0,18,99\r", "66,ILLEGAL TRACK OR SECTOR,18,99\r");
    expect_command_response(testname, dr, "U2:2,0,36,0\r", "66,ILLEGAL TRACK OR SECTOR,36,00\r");
    expect_command_response(testname, dr, "B-A:0,40,1\r", "66,ILLEGAL TRACK OR SECTOR,40,01\r");
    close_file(dr, 2);
}

// B-A of a block that is already allocated answers 65, NO BLOCK, with the next higher
// free track and sector, or track 0 when no higher block is free (1541-II User's Guide,
// error 65; HD 9-43 and appendix B). B-F of a block that is already free changes nothing
// and is not an error; neither manual lists one. Both answered 74, DRIVE NOT READY.
// SI-091a: B-A of an allocated block answers 65 with the next free one, and B-F of a
// free block answers OK.
static void s11_block_allocate_answers(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-BlockAllocateAnswers";
    const char *image = "/Fat/s11_ba.d64";
    create_formatted_image(fm, image, "ALLOC", 683, e_image_d64);
    dr->add_partition(42, image, "ALLOC");
    expect_command_response(testname, dr, "CP42\r", "02,PARTITION SELECTED,42,00\r");
    uint8_t listing[4096];
    int got = read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    int free_before = listing[got - 30] | (listing[got - 29] << 8); // the BLOCKS FREE line

    // A formatted disk holds the BAM at 18/0 and the first directory block at 18/1.
    expect_command_response(testname, dr, "B-A:0,18,0\r", "65,NO BLOCK,18,02\r");
    expect_command_response(testname, dr, "B-A:0,1,0\r", "00, OK,00,00\r");
    expect_command_response(testname, dr, "B-A:0,1,0\r", "65,NO BLOCK,01,01\r");
    got = read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    REQUIRE((listing[got - 30] | (listing[got - 29] << 8)) == free_before - 1);
    expect_command_response(testname, dr, "B-F:0,1,0\r", "00, OK,00,00\r");
    expect_command_response(testname, dr, "B-F:0,1,0\r", "00, OK,00,00\r");
    got = read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    REQUIRE((listing[got - 30] | (listing[got - 29] << 8)) == free_before);

    // The last block of track 35 has no higher block after it.
    expect_command_response(testname, dr, "B-A:0,35,16\r", "00, OK,00,00\r");
    expect_command_response(testname, dr, "B-A:0,35,16\r", "65,NO BLOCK,00,00\r");
    expect_command_response(testname, dr, "B-F:0,35,16\r", "00, OK,00,00\r");
    got = read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    REQUIRE((listing[got - 30] | (listing[got - 29] << 8)) == free_before);
}

// SI-033: a scratch that matches nothing is not an error. The answer is 01 with a
// count of zero, as HD B-1 and the 1541 give it. C64 OS sends S/TEMPORARY/:* on every
// boot, whether or not the directory holds anything.
static void s11_si033_scratch_nothing(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI033-ScratchNothing";
    s11_partition(fm, dr, "si033");
    expect_command_response(testname, dr, "S:NOSUCHFILE\r", "01, FILES SCRATCHED,00,00\r");
    expect_command_ok(testname, dr, "MD:TEMPORARY\r");
    expect_command_response(testname, dr, "S/TEMPORARY/:*\r", "01, FILES SCRATCHED,00,00\r");
    // The count still counts.
    expect_iec_write_ok(testname, dr, 2, "/TEMPORARY/:GONE,S,W", "x");
    expect_command_response(testname, dr, "S/TEMPORARY/:*\r", "01, FILES SCRATCHED,01,00\r");
    // A directory that is not there is a path error, not a scratch of nothing.
    expect_command_response(testname, dr, "S/NOSUCHDIR/:*\r", "71,DIRECTORY ERROR,40,00\r");
}

// SI-053: I initialises. There is no medium to read in, so it answers OK, and like
// sd2iec it closes the channels a program left open, flushing what was written to
// them. UI is a different command and still answers with the DOS version.
static void s11_si053_initialize(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI053-Initialize";
    s11_partition(fm, dr, "si053");
    expect_command_response(testname, dr, "I\r", "00, OK,00,00\r");
    expect_command_response(testname, dr, "I0:\r", "00, OK,00,00\r");
    expect_command_response(testname, dr, "UI\r", "73,U64HD ULTIMATE DOS V2.0,00,00\r");

    // A file written and never closed: I closes it, so what was written is there.
    open_file(dr, 3, "LEFTOPEN,S,W");
    get_status(dr);
    expect_status_ok(testname, "LEFTOPEN,S,W");
    send_channel_data(dr, 3, (const uint8_t *)"FLUSHED", 7);
    expect_command_response(testname, dr, "I\r", "00, OK,00,00\r");
    expect_iec_file(testname, dr, 2, "LEFTOPEN,S,R", "FLUSHED");
}

// SI-045 and SI-046: the partition directory is a header, one line per partition and
// the BASIC end marker, with no blocks free line (issue #890), and the number in front
// of the header name is the number of partitions.
static void s11_si045_partition_directory(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI045-PartitionDirectory";
    s11_partition(fm, dr, "si045");
    int partitions = 0;
    for (int i = 1; i < MAX_PARTITIONS; i++) {
        IecPartition *p = dr->get_file_system()->GetPartition(i);
        if (p && (p->GetPartitionNumber() == i)) {
            partitions++;
        }
    }
    uint8_t listing[8192];
    int got = read_directory_stream(testname, dr, "$=P", listing, sizeof(listing));
    if (memmem(listing, got, "BLOCKS FREE", 11)) {
        printf("%s: the partition directory has a blocks free line\n", testname);
        dump_hex_relative(listing + got - 64, 64);
    }
    REQUIRE(memmem(listing, got, "BLOCKS FREE", 11) == NULL);
    // A header and one 32 byte line per partition, then the two zero bytes that end a
    // BASIC program.
    if (got != (32 * (partitions + 1)) + 2) {
        printf("%s: %d bytes for %d partitions, expected %d\n", testname, got, partitions,
               (32 * (partitions + 1)) + 2);
    }
    REQUIRE(got == (32 * (partitions + 1)) + 2);
    REQUIRE((listing[got - 2] == 0) && (listing[got - 1] == 0));
}

static void s11_si046_partition_count(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI046-PartitionCount";
    s11_partition(fm, dr, "si046");
    int partitions = 0;
    for (int i = 1; i < MAX_PARTITIONS; i++) {
        IecPartition *p = dr->get_file_system()->GetPartition(i);
        if (p && (p->GetPartitionNumber() == i)) {
            partitions++;
        }
    }
    uint8_t listing[8192];
    read_directory_stream(testname, dr, "$=P", listing, sizeof(listing));
    // Bytes 4 and 5 of the header are the line number BASIC prints before the name.
    int shown = listing[4] | (listing[5] << 8);
    if (shown != partitions) {
        printf("%s: header shows %d, there are %d partitions\n", testname, shown, partitions);
    }
    REQUIRE(partitions > 1);
    REQUIRE(shown == partitions);
}

// Reads what the command channel has to say, however long it is. last_status only
// holds a status line, and M-R can answer with 256 bytes.
static int read_command_channel(IecDrive *dr, uint8_t *out, int size)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x6F);
    dr->talk();
    int total = 0;
    while (total < size) {
        uint8_t *data;
        int n = 0;
        t_channel_retval ret = dr->prefetch_more(256, data, n);
        if (n > size - total) {
            n = size - total;
        }
        memcpy(out + total, data, n);
        total += n;
        dr->pop_more(n);
        if ((ret != IEC_OK) || (n == 0)) {
            break;
        }
    }
    return total;
}

// SI-100: U0>+CHR$(d) moves the drive to device number d for as long as it runs; the
// configured number is not written.
static void s11_si100_device_number(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI100-DeviceNumber";
    s11_partition(fm, dr, "si100");
    int configured = dr->get_address();
    const uint8_t u0_12[5] = { 'U', '0', '>', 12, 0x0D };
    expect_command_data_response(testname, dr, u0_12, sizeof(u0_12), "00, OK,00,00\r");
    printf("%s: device number now %d, configured %d\n", testname, dr->get_address(), configured);
    REQUIRE(dr->get_address() == 12);
    // The drive keeps answering on the command channel.
    expect_command_response(testname, dr, "UI\r", "73,U64HD ULTIMATE DOS V2.0,00,00\r");
    // Outside 8 to 30 nothing changes.
    const uint8_t u0_7[4] = { 'U', '0', '>', 7 };
    expect_command_data_response(testname, dr, u0_7, sizeof(u0_7), "30,SYNTAX ERROR,00,00\r");
    REQUIRE(dr->get_address() == 12);
    const uint8_t u0_back[4] = { 'U', '0', '>', (uint8_t)configured };
    expect_command_data_response(testname, dr, u0_back, sizeof(u0_back), "00, OK,00,00\r");
    REQUIRE(dr->get_address() == configured);
}

// SI-105 and SI-112: M-R answers the number of bytes asked for, all zero, and does not
// read past the end of the page; M-W and M-E are refused, because no drive code runs here.
// SI-105, SI-110, SI-111, SI-112, SI-113, SI-114: M-R answers zeros, which is no
// drive's signature, and UI is the identification.
static void s11_si105_memory_commands(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI105-MemoryCommands";
    s11_partition(fm, dr, "si105");
    uint8_t reply[512];

    // The four probes C64 OS sends during its boot, two bytes each, as PRINT# sends them.
    static const uint16_t probes[] = { 0xFEA4, 0xE5C5, 0xA6E8, 0x0002 };
    for (int i = 0; i < 4; i++) {
        uint8_t cmd[7] = { 'M', '-', 'R', (uint8_t)(probes[i] & 0xFF), (uint8_t)(probes[i] >> 8), 2, 0x0D };
        send_command_data(dr, cmd, sizeof(cmd));
        memset(reply, 0xAA, sizeof(reply));
        int got = read_command_channel(dr, reply, sizeof(reply));
        printf("%s: M-R $%04X,2 answered %d bytes: %02X %02X\n", testname, probes[i], got, reply[0], reply[1]);
        REQUIRE(got == 2);
        REQUIRE((reply[0] == 0) && (reply[1] == 0));
    }
    // After the bytes, the error channel says OK again.
    get_status(dr);
    expect_current_status(testname, "after M-R", "00, OK,00,00\r");

    // No count reads one byte, a count of zero reads 256, and the page end stops it.
    const uint8_t one[5] = { 'M', '-', 'R', 0x00, 0xFE };
    send_command_data(dr, one, sizeof(one));
    REQUIRE(read_command_channel(dr, reply, sizeof(reply)) == 1);
    const uint8_t all[6] = { 'M', '-', 'R', 0x00, 0xFE, 0x00 };
    send_command_data(dr, all, sizeof(all));
    int got = read_command_channel(dr, reply, sizeof(reply));
    printf("%s: M-R $FE00,0 answered %d bytes\n", testname, got);
    REQUIRE(got == 256);
    for (int i = 0; i < 256; i++) {
        REQUIRE(reply[i] == 0);
    }
    const uint8_t edge[6] = { 'M', '-', 'R', 0xF0, 0xFE, 0x20 };
    send_command_data(dr, edge, sizeof(edge));
    REQUIRE(read_command_channel(dr, reply, sizeof(reply)) == 16);

    const uint8_t mw[8] = { 'M', '-', 'W', 0x00, 0x05, 0x02, 0xEA, 0x60 };
    expect_command_data_response(testname, dr, mw, sizeof(mw), "30,SYNTAX ERROR,00,00\r");
    const uint8_t me[5] = { 'M', '-', 'E', 0x00, 0x05 };
    expect_command_data_response(testname, dr, me, sizeof(me), "30,SYNTAX ERROR,00,00\r");
}

// SI-021: a command, and the name of a file opened on a data channel, can be longer
// than 64 bytes. C64 OS paths reach 232 characters.
static void s11_si021_long_names(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI021-LongNames";
    s11_partition(fm, dr, "si021");
    expect_command_ok(testname, dr, "MD:DIRECTORYNAMENUMBERONEXYZ\r");
    expect_command_ok(testname, dr, "MD/DIRECTORYNAMENUMBERONEXYZ/:DIRECTORYNAMENUMBERTWOXYZ\r");
    const char *file = "/DIRECTORYNAMENUMBERONEXYZ/DIRECTORYNAMENUMBERTWOXYZ/:VICTIMFILE";
    char name[128];
    snprintf(name, sizeof(name), "%s,S,W", file);
    REQUIRE(strlen(name) > 64);
    expect_iec_write_ok(testname, dr, 2, name, "long");
    snprintf(name, sizeof(name), "%s,S,R", file);
    expect_iec_file(testname, dr, 2, name, "long");
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "S%s\r", file);
    REQUIRE(strlen(cmd) > 64);
    expect_command_response(testname, dr, cmd, "01, FILES SCRATCHED,01,00\r");
}

// SI-022: a command longer than the buffer answers 32 and is not executed. Cut short
// and executed, a scratch removes what its first bytes name.
static void s11_si022_too_long(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI022-TooLong";
    const char *path = s11_partition(fm, dr, "si022");
    expect_iec_write_ok(testname, dr, 2, "VICTIM,S,W", "keep me");
    char cmd[300];
    memset(cmd, 'A', sizeof(cmd));
    memcpy(cmd, "S:VICTIM,", 9);
    cmd[255] = 0;
    expect_command_response(testname, dr, cmd, "32,SYNTAX ERROR,00,00\r");
    expect_iec_file(testname, dr, 2, "VICTIM,S,R", "keep me");
    cmd[254] = 0; // a command that fills the buffer is refused as well
    expect_command_response(testname, dr, cmd, "32,SYNTAX ERROR,00,00\r");
    expect_iec_file(testname, dr, 2, "VICTIM,S,R", "keep me");
    cmd[253] = 0; // one byte shorter fits
    expect_command_response(testname, dr, cmd, "01, FILES SCRATCHED,01,00\r");

    // A name that fills the buffer is refused the same way, not created under its first
    // bytes.
    char name[300];
    memset(name, 'B', sizeof(name));
    memcpy(name, "LONGNAME", 8);
    name[260] = 0;
    open_file(dr, 2, name);
    get_status(dr);
    expect_current_status(testname, "a 260 byte name", "32,SYNTAX ERROR,00,00\r");
    close_file(dr, 2);
    uint8_t listing[4096];
    int got = read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    REQUIRE(memmem(listing, got, "LONGNAME", 8) == NULL);
}

// SI-016: a command ending in a carriage return and a line feed, as PRINT# to a logical
// file number of 128 or more sends it, ends before them.
static void s11_si016_second_terminator(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI016-SecondTerminator";
    s11_partition(fm, dr, "si016");
    expect_command_ok(testname, dr, "MD:SUB\r");
    expect_command_ok(testname, dr, "CD:SUB\r\n");
    expect_command_response(testname, dr, "XPWD\r", "40:/SUB/");
}

// SI-018: the position command keeps a last byte of 13, because it is an offset and
// not a terminator.
static void s11_si018_position_exempt(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI018-PositionExempt";
    s11_partition(fm, dr, "si018");
    const uint8_t chan = 3;
    const uint8_t record[16] = { 'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P' };
    expect_rel_open(testname, dr, chan, "RECORDS", sizeof(record));
    expect_rel_write(testname, dr, chan, record, sizeof(record));
    close_file(dr, chan);
    expect_rel_open(testname, dr, chan, "RECORDS", sizeof(record));
    // P, the channel, record 1, offset 13, and no terminator: JiffyDOS's @ sends this.
    const uint8_t p13[5] = { 'P', chan, 1, 0, 13 };
    expect_command_data_response(testname, dr, p13, sizeof(p13), "00, OK,00,00\r");
    expect_rel_read(testname, dr, chan, record + 12, 4);
    // The same from BASIC, with the terminator behind it.
    const uint8_t p13_basic[6] = { 'P', (uint8_t)(96 + chan), 1, 0, 13, 0x0D };
    expect_command_data_response(testname, dr, p13_basic, sizeof(p13_basic), "00, OK,00,00\r");
    expect_rel_read(testname, dr, chan, record + 12, 4);
    close_file(dr, chan);
}

// SI-014 and SI-015: the left arrow (0x5F) is the parent directory in the name
// position, directly after CD[n] or after a colon, and an ordinary character between
// slashes. This is the reproduction from #877.
static void s11_si014_left_arrow(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI014-LeftArrow";
    const char *path = s11_partition(fm, dr, "si014");
    char host[80];
    expect_command_ok(testname, dr, "MD:_\r");
    snprintf(host, sizeof(host), "%s/_", path);
    expect_path_exists(testname, fm, host);
    expect_directory_contains(testname, dr, "$", "\"_\"");
    expect_command_ok(testname, dr, "CD/_\r");
    expect_command_response(testname, dr, "XPWD\r", "40:/_/");
    expect_command_ok(testname, dr, "CD:_\r");
    expect_command_response(testname, dr, "XPWD\r", "40:/");
    expect_command_ok(testname, dr, "CD/_\r");
    expect_command_ok(testname, dr, "CD_\r");
    expect_command_response(testname, dr, "XPWD\r", "40:/");
    expect_command_ok(testname, dr, "CD/_\r");
    expect_command_ok(testname, dr, "CD/:_\r"); // SI-015
    expect_command_response(testname, dr, "XPWD\r", "40:/");
    // A left arrow between slashes is a name; .. goes up and down again.
    expect_command_ok(testname, dr, "MD/_/:OTHER\r");
    expect_command_ok(testname, dr, "CD//_/OTHER\r");
    expect_command_response(testname, dr, "XPWD\r", "40:/_/OTHER/");
    expect_command_ok(testname, dr, "CD/../OTHER\r");
    expect_command_response(testname, dr, "XPWD\r", "40:/_/OTHER/");
    expect_command_ok(testname, dr, "CD//_\r");
    expect_command_ok(testname, dr, "RD:OTHER\r");
    expect_command_ok(testname, dr, "CD:_\r");
    expect_command_ok(testname, dr, "RD:_\r");
    expect_path_absent(testname, fm, host);
}

// SI-012: a path component may carry wildcards, and the first match is used.
static void s11_si012_wildcard_path(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI012-WildcardPath";
    s11_partition(fm, dr, "si012");
    expect_command_ok(testname, dr, "MD:SUBDIRECTORY\r");
    expect_command_ok(testname, dr, "MD/SUBDIRECTORY/:DEEP\r");
    expect_command_ok(testname, dr, "CD//SUB*/DEEP\r");
    expect_command_response(testname, dr, "XPWD\r", "40:/SUBDIRECTORY/DEEP/");
    expect_command_ok(testname, dr, "CD//SUBDIRECTOR?/\r");
    expect_command_response(testname, dr, "XPWD\r", "40:/SUBDIRECTORY/");
    expect_command_ok(testname, dr, "CD//\r");
    expect_iec_write_ok(testname, dr, 2, "/SUBDIRECTORY/DEEP/:INSIDE,S,W", "found");
    expect_iec_file(testname, dr, 2, "//S*/D*/:INSIDE,S,R", "found");
}

// SI-060: MD needs a colon, and a name that is a shifted space is no name.
static void s11_si060_md_colon(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI060-MdColon";
    s11_partition(fm, dr, "si060");
    expect_command_response(testname, dr, "MD NOCOLON\r", "34,SYNTAX ERROR,00,00\r");
    const uint8_t shifted_space[5] = { 'M', 'D', ':', 0xA0, 0x0D };
    expect_command_data_response(testname, dr, shifted_space, sizeof(shifted_space), "34,SYNTAX ERROR,00,00\r");
    expect_command_ok(testname, dr, "MD:WITHCOLON\r");
}

// SI-063: RD takes no path, so it cannot remove the parent of where the user stands.
static void s11_si063_rd_no_path(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI063-RdNoPath";
    const char *path = s11_partition(fm, dr, "si063");
    char host[80];
    expect_command_ok(testname, dr, "MD:PROBEDIR\r");
    expect_command_response(testname, dr, "RD/PROBEDIR\r", "34,SYNTAX ERROR,00,00\r");
    expect_command_response(testname, dr, "RD//:PROBEDIR\r", "34,SYNTAX ERROR,00,00\r");
    snprintf(host, sizeof(host), "%s/PROBEDIR", path);
    expect_path_exists(testname, fm, host);
    expect_command_ok(testname, dr, "RD:PROBEDIR\r");
    expect_path_absent(testname, fm, host);
}

// SI-150: a path deeper than sixteen components is followed to its end, not cut off.
static void s11_si150_deep_path(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI150-DeepPath";
    s11_partition(fm, dr, "si150");
    char path[128] = "";
    char cmd[160];
    for (int i = 0; i < 20; i++) {
        snprintf(cmd, sizeof(cmd), "MD//%s:%c\r", path, 'A' + i);
        expect_command_ok(testname, dr, cmd);
        int len = strlen(path);
        path[len] = 'A' + i;
        path[len + 1] = '/';
        path[len + 2] = 0;
    }
    snprintf(cmd, sizeof(cmd), "CD//%s\r", path);
    expect_command_ok(testname, dr, cmd);
    char pwd[160];
    snprintf(pwd, sizeof(pwd), "40:/%s", path);
    expect_command_response(testname, dr, "XPWD\r", pwd);
}

// SI-130 and SI-131: a listing is a BASIC program loaded at $0401, so it starts with
// the load address 01 04 and a link 01 01; byte 4 is the partition number.
static void s11_si130_listing_header(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI130-ListingHeader";
    s11_partition(fm, dr, "si130");
    uint8_t listing[4096];
    read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    printf("%s: first bytes %02X %02X %02X %02X %02X\n", testname,
           listing[0], listing[1], listing[2], listing[3], listing[4]);
    REQUIRE((listing[0] == 0x01) && (listing[1] == 0x04));
    REQUIRE((listing[2] == 0x01) && (listing[3] == 0x01));
    REQUIRE(listing[4] == 40);
}

// The line of a listing whose quoted name is `name`, or NULL.
static const uint8_t *s11_listing_line(const uint8_t *listing, int length, const char *name)
{
    char quoted[24];
    snprintf(quoted, sizeof(quoted), "\"%s\"", name);
    for (int offset = 32; offset + 32 <= length; offset += 32) {
        if (memmem(listing + offset, 32, quoted, strlen(quoted))) {
            return listing + offset;
        }
    }
    return NULL;
}

// SI-133: the low byte of each line's link is (file size mod 254) + 2, so a program can
// work out a file's exact length from the listing.
static void s11_si133_size_remainder(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI133-SizeRemainder";
    const char *path = s11_partition(fm, dr, "si133");
    uint32_t tr;
    uint8_t data[600];
    memset(data, 'x', sizeof(data));
    REQUIRE(fm->save_file(true, path, "TEN.prg", data, 10, &tr) == FR_OK);
    REQUIRE(fm->save_file(true, path, "EXACT.prg", data, 254, &tr) == FR_OK);
    REQUIRE(fm->save_file(true, path, "LONGER.prg", data, 300, &tr) == FR_OK);
    uint8_t listing[4096];
    int got = read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    static const struct { const char *name; uint8_t link; } cases[] = {
        { "TEN", 12 }, { "EXACT", 2 }, { "LONGER", 48 },
    };
    for (int i = 0; i < 3; i++) {
        const uint8_t *line = s11_listing_line(listing, got, cases[i].name);
        REQUIRE(line != NULL);
        printf("%s: %s link %02X %02X, expected %02X 01\n", testname, cases[i].name, line[0], line[1], cases[i].link);
        REQUIRE((line[0] == cases[i].link) && (line[1] == 0x01));
    }
}

// One GET#, as BASIC performs it: address the channel to talk, take a single byte, and
// release the bus again. The drive sees a fresh talk for every byte, which is what
// separates this from a load (SI-138, issue #917).
static t_channel_retval s11_get_byte(IecDrive *dr, uint8_t chan, uint8_t *out)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x60 | chan);
    dr->talk();
    uint8_t data = 0;
    t_channel_retval ret = dr->prefetch_data(data);
    if ((ret == IEC_OK) || (ret == IEC_LAST)) {
        *out = data;
        dr->pop_data();
    }
    return ret;
}

// SI-138: the last byte of a listing carries EOI, whatever the size of the reads that
// brought the listing in. Reported on issue #917: a BASIC program that reads a listing
// with GET# never saw a status of 64 and read zeroes for ever. The tail is read one byte
// per talk here, which is the case that failed; a whole-stream read never does.
static void s11_si138_listing_eof(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI138-ListingEof";
    const char *path = s11_partition(fm, dr, "si138");
    uint32_t tr;
    uint8_t data[16];
    memset(data, 'e', sizeof(data));
    REQUIRE(fm->save_file(true, path, "ONE.prg", data, sizeof(data), &tr) == FR_OK);
    REQUIRE(fm->save_file(true, path, "TWO.seq", data, sizeof(data), &tr) == FR_OK);

    static const char *listings[] = { "$", "$=T:*", "$=T:*=L", "$=P" };
    for (int i = 0; i < 4; i++) {
        const char *name = listings[i];
        uint8_t listing[4096];
        int total = read_directory_stream(testname, dr, name, listing, sizeof(listing));
        REQUIRE(total > 4);

        // Everything but the last four bytes in one piece, then the tail one byte at a
        // time. The fourth of those bytes is the last byte of the listing.
        open_file(dr, 0, name);
        get_status(dr);
        expect_status_ok(testname, name);
        REQUIRE(read_file_limited(dr, 0, NULL, total - 4) == (total - 4));

        int reads = 0;
        t_channel_retval ret = IEC_OK;
        uint8_t byte = 0xFF;
        while ((reads < 16) && (ret == IEC_OK)) {
            ret = s11_get_byte(dr, 0, &byte);
            reads++;
        }
        printf("%s: %s ends after %d single byte reads with %d\n", testname, name, reads, (int)ret);
        REQUIRE(ret == IEC_LAST);
        REQUIRE(reads == 4);
        REQUIRE(byte == 0);
        close_file(dr, 0);
        expect_status_ok(testname, name);
    }
}

// The time stamp a line of `length` bytes carries, given the block count in front of it.
// The type field sits at a fixed printed column, so everything behind it moves with the
// number of digits BASIC prints for the line number.
static void s11_expect_stamped_line(const char *testname, const uint8_t *line, int length,
                                    const char *type, int gap, int stamp_len)
{
    int blocks = line[2] | (line[3] << 8);
    int chars = (blocks >= 1000) ? 4 : (blocks >= 100) ? 3 : (blocks >= 10) ? 2 : 1;
    int type_at = 27 - chars;
    int stamp_at = type_at + (int)strlen(type) + gap;
    printf("%s: %d blocks, type at %d, stamp at %d: ", testname, blocks, type_at, stamp_at);
    for (int i = 0; i < length; i++) {
        printf("%02X ", line[i]);
    }
    printf("\n");
    REQUIRE(memcmp(line + type_at, type, strlen(type)) == 0);
    for (int i = type_at + (int)strlen(type); i < stamp_at; i++) {
        REQUIRE(line[i] == 32);
    }
    REQUIRE((line[stamp_at + 2] == '/') && (line[stamp_at + 5] == '/' || line[stamp_at + 5] == ' '));
    // The filler CMD DOS puts behind the stamp, and the zero that ends the BASIC line.
    for (int i = stamp_at + stamp_len; i < length - 1; i++) {
        REQUIRE(line[i] == 1);
    }
    REQUIRE(line[length - 1] == 0);
}

// SI-135, SI-139: a time stamped listing in both formats, and the line it produces: a
// fixed 64 bytes in the long format, 42 in the short one, with 0x01 behind the stamp.
static void s11_si139_stamped_entries(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI139-StampedEntries";
    const char *path = s11_partition(fm, dr, "si139");
    uint32_t tr;
    uint8_t data[3000];
    memset(data, 'x', sizeof(data));
    // One block, so BASIC prints one digit, and twelve blocks, so it prints two. The two
    // digit case is the one the report names: one filler byte in the short format.
    REQUIRE(fm->save_file(true, path, "SMALL.prg", data, 16, &tr) == FR_OK);
    REQUIRE(fm->save_file(true, path, "BIGGER.prg", data, sizeof(data), &tr) == FR_OK);

    static const struct { const char *name; int line; const char *type; int gap; int stamp; }
    cases[] = {
        { "$=T:*=L", 64, "PRG", 3, 19 }, // MM/DD/YY   HH.MM xM
        { "$=T:*",   42, "P",   1, 13 }, // MM/DD HH.MM x
    };
    for (int i = 0; i < 2; i++) {
        uint8_t listing[4096];
        int got = read_directory_stream(testname, dr, cases[i].name, listing, sizeof(listing));
        // A header and a blocks free line of 32 bytes, and two lines in between.
        printf("%s: %s is %d bytes\n", testname, cases[i].name, got);
        REQUIRE(got == 64 + 2 * cases[i].line);
        for (int entry = 0; entry < 2; entry++) {
            s11_expect_stamped_line(testname, listing + 32 + entry * cases[i].line,
                                    cases[i].line, cases[i].type, cases[i].gap, cases[i].stamp);
        }
    }
}

// SI-149: a GEOS file in a mounted image is listed with the type its own directory entry
// carries, and a read over the bus delivers the file rather than the CVT container the
// file browser builds around it. Reported on issue #917, where a GEOS disk mapped as a
// partition listed every file as SEQ. The extension the image file system gives a GEOS
// entry is CVT, which the IEC name builder did not recognise, so the type fell back to
// SEQ; the open then took the branch that puts a header block in front of the data.
//
// The read half of this case is a regression guard. 3.14 and 3.14d passed
// FA_OPEN_FROM_CBM from the IEC open and received the file; commit 76d887bd rewrote that
// open and dropped the argument, so 3.15 received the CVT container. Anyone rewriting
// setup_file_access() again fails here rather than shipping the same loss twice.
static void s11_si149_geos_entries(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI149-GeosEntries";
    const char *path = s11_partition(fm, dr, "si149");
    create_iec_geos_fixture("output/iec_geos_cases.d64");
    char host[96];
    snprintf(host, sizeof(host), "%s/GEOS.D64", path);
    REQUIRE(copy_to("output/iec_geos_cases.d64", host) == FR_OK);
    expect_command_ok(testname, dr, "CD:GEOS.D64\r");

    // The listed type, taken from each entry's own type bits.
    static const struct { const char *name; const char *type; } listed[] = {
        { "\"GEOSPRG\"",  "PRG" },
        { "\"GEOSSEQ\"",  "SEQ" },
        { "\"GEOSUSR\"",  "USR" },
        { "\"PLAINPRG\"", "PRG" },
    };
    for (int i = 0; i < 4; i++) {
        // Every entry is one block, so BASIC prints one digit and the type sits at a
        // fixed column: the gap behind the name shortens as the name grows.
        char expected[48];
        snprintf(expected, sizeof(expected), "%s%*s%s", listed[i].name,
                 (int)(19 - strlen(listed[i].name)), "", listed[i].type);
        expect_directory_contains(testname, dr, "$", expected);
    }

    // The bytes a read delivers. A CVT stream starts with the directory entry, so its
    // first byte would be the type byte 0x82 and not the load address of the file.
    expect_iec_file(testname, dr, 2, "GEOSPRG", "\x01\x08GEOS:PRG");
    expect_iec_file(testname, dr, 2, "GEOSSEQ", "GEOS:SEQ");
    expect_iec_file(testname, dr, 2, "PLAINPRG", "\x01\x08PLAIN:PRG");

    // A VLIR file points at a record block, which a drive hands over as a whole sector.
    // Before the change this read answered nothing and logged a channel fault.
    uint8_t vlir[512];
    memset(vlir, 0, sizeof(vlir));
    open_file(dr, 2, "GEOSUSR");
    get_status(dr);
    expect_status_ok(testname, "GEOSUSR");
    int got = read_file(dr, 2, vlir, sizeof(vlir));
    printf("%s: GEOSUSR read %d bytes, first four %02x %02x %02x %02x\n",
           testname, got, vlir[0], vlir[1], vlir[2], vlir[3]);
    REQUIRE(got == 254);
    REQUIRE(vlir[0] == 17);
    REQUIRE(vlir[1] == 11);
    REQUIRE(memcmp(vlir + 2, "VLIR", 4) == 0);
    close_file(dr, 2);
    expect_status_ok(testname, "GEOSUSR");
}

// SI-134: $:*=H lists what $:* lists; H shows hidden files and filters nothing out.
static void s11_si134_hidden_flag(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI134-HiddenFlag";
    const char *path = s11_partition(fm, dr, "si134");
    uint32_t tr;
    REQUIRE(fm->save_file(true, path, "VISIBLE.prg", (const uint8_t *)"V", 1, &tr) == FR_OK);
    REQUIRE(fm->save_file(true, path, "SECRET.prg", (const uint8_t *)"S", 1, &tr) == FR_OK);
    expect_command_ok(testname, dr, "MD:FOLDER\r");

    // Nothing is hidden yet, so both listings carry every entry.
    uint8_t all[4096], hidden[4096];
    int n_all = read_directory_stream(testname, dr, "$:*", all, sizeof(all));
    int n_hidden = read_directory_stream(testname, dr, "$:*=H", hidden, sizeof(hidden));
    printf("%s: $:* is %d bytes, $:*=H is %d bytes\n", testname, n_all, n_hidden);
    REQUIRE(s11_listing_line(hidden, n_hidden, "VISIBLE") != NULL);
    REQUIRE(s11_listing_line(hidden, n_hidden, "FOLDER") != NULL);
    REQUIRE(n_all == n_hidden);

    // A hidden entry is left out until the filter asks for it, and the flag turns back.
    expect_command_ok(testname, dr, "EHSECRET\r");
    n_all = read_directory_stream(testname, dr, "$:*", all, sizeof(all));
    n_hidden = read_directory_stream(testname, dr, "$:*=H", hidden, sizeof(hidden));
    REQUIRE(s11_listing_line(all, n_all, "SECRET") == NULL);
    REQUIRE(s11_listing_line(all, n_all, "VISIBLE") != NULL);
    REQUIRE(s11_listing_line(hidden, n_hidden, "SECRET") != NULL);
    // A hidden entry in a listing that asks for it carries an H behind the lock mark,
    // as SD createentry() writes it (SI-132).
    const uint8_t *secret = s11_listing_line(hidden, n_hidden, "SECRET");
    REQUIRE(memcmp(secret + 4, "   \"SECRET\"           PRG H", 27) == 0);
    REQUIRE(memcmp(s11_listing_line(hidden, n_hidden, "VISIBLE") + 4,
                   "   \"VISIBLE\"          PRG  ", 27) == 0);
    // A hidden entry still answers to its name, which is the only way to reach it and
    // to turn the flag back.
    expect_iec_file(testname, dr, 0, "SECRET", "S");
    expect_command_ok(testname, dr, "EHSECRET\r");
    n_all = read_directory_stream(testname, dr, "$:*", all, sizeof(all));
    REQUIRE(s11_listing_line(all, n_all, "SECRET") != NULL);
}

// SI-065: the header of a listing carries the listed directory's own name, and the
// partition's name only at the root.
static void s11_si065_header_name(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI065-HeaderName";
    s11_partition(fm, dr, "si065");
    expect_command_ok(testname, dr, "MD:GAMES\r");
    expect_command_ok(testname, dr, "MD/GAMES/:ACTION\r");
    uint8_t listing[4096];
    char header[17];
    header[16] = 0;
    read_directory_stream(testname, dr, "$/GAMES/", listing, sizeof(listing));
    memcpy(header, listing + 8, 16);
    printf("%s: header of $/GAMES/ is '%s'\n", testname, header);
    REQUIRE(memcmp(listing + 8, "GAMES           ", 16) == 0);
    expect_command_ok(testname, dr, "CD//GAMES/ACTION\r");
    read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    memcpy(header, listing + 8, 16);
    printf("%s: header of $ in ACTION is '%s'\n", testname, header);
    REQUIRE(memcmp(listing + 8, "ACTION          ", 16) == 0);
    read_directory_stream(testname, dr, "$//", listing, sizeof(listing));
    memcpy(header, listing + 8, 16);
    printf("%s: header of $// is '%s'\n", testname, header);
    REQUIRE(memcmp(listing + 8, "SUITE11         ", 16) == 0);
}

// SI-032 and SI-148: opening a name for writing. A wildcard without @ is an illegal
// name (33). With @, the first match is replaced under its own name when its type is
// the one asked for, and anything else answers 64: another type, a relative file, or no
// match at all (1541 ROM $D8F5). A name that starts with a shifted space is refused the
// same way.
static void s11_si032_wildcard_write(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI032-WildcardWrite";
    const char *path = s11_partition(fm, dr, "si032");
    char host[80];
    expect_iec_write_ok(testname, dr, 1, "FOOBAR", "old");
    expect_iec_write_ok(testname, dr, 2, "FOOSEQ,S,W", "seq");

    // @ and a pattern matching a PRG, from a SAVE: the PRG is replaced, name and all.
    expect_iec_write_ok(testname, dr, 1, "@:FOOB*", "new");
    expect_iec_file(testname, dr, 0, "FOOBAR", "new");
    snprintf(host, sizeof(host), "%s/FOOB*.prg", path);
    expect_path_absent(testname, fm, host);
    // @ and a pattern whose first match is not the type asked for.
    expect_iec_open_status_prefix(testname, dr, 1, "@:FOOS*", "64,FILE TYPE MISMATCH");
    close_file(dr, 1);
    expect_iec_file(testname, dr, 2, "FOOSEQ,S,R", "seq");
    // @ and a pattern that matches nothing.
    expect_iec_open_status_prefix(testname, dr, 1, "@:NOMATCH*", "64,FILE TYPE MISMATCH");
    close_file(dr, 1);
    // @ and a pattern matching a relative file.
    expect_rel_open(testname, dr, 3, "FOOREL", 16);
    close_file(dr, 3);
    expect_iec_open_status_prefix(testname, dr, 2, "@:FOOR*,S,W", "64,FILE TYPE MISMATCH");
    close_file(dr, 2);
    // No @: a wildcard in a name to write is an illegal name.
    expect_iec_open_status_prefix(testname, dr, 1, "FOO*", "33,SYNTAX ERROR");
    close_file(dr, 1);
    expect_iec_open_status_prefix(testname, dr, 2, "FOO?AR,S,W", "33,SYNTAX ERROR");
    close_file(dr, 2);
    // SI-148: a name that starts with a shifted space is no name to create.
    expect_iec_open_status_prefix(testname, dr, 2, "@:\xA0NAME,S,W", "64,FILE TYPE MISMATCH");
    close_file(dr, 2);
    expect_iec_open_status_prefix(testname, dr, 2, "\xA0NAME,S,W", "33,SYNTAX ERROR");
    close_file(dr, 2);
}

// SI-041 and SI-042: G-P with 0 asks about the system partition, which this drive does
// not have; bytes 27 to 29 are the partition's size in 512 byte blocks; byte 1 is 0.
static void s11_si041_partition_size(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI041-PartitionSize";
    s11_partition(fm, dr, "si041");
    const uint8_t system[4] = { 'G', '-', 'P', 0 };
    send_command_data(dr, system, sizeof(system));
    get_status(dr);
    printf("%s: G-P 0 answered %d bytes, type %d, partition %d\n", testname, last_status_size,
           (uint8_t)last_status[0], (uint8_t)last_status[2]);
    REQUIRE(last_status_size == 31);
    REQUIRE((last_status[0] == 0) && (last_status[1] == 0) && (last_status[2] == 0));

    // A D64 image is 174848 bytes, 342 blocks of 512 rounded up.
    create_formatted_image(fm, "/Fat/s11_si041.d64", "SIZED", 683, e_image_d64);
    dr->add_partition(44, "/Fat/s11_si041.d64", "SIZED");
    const uint8_t gp44[4] = { 'G', '-', 'P', 44 };
    send_command_data(dr, gp44, sizeof(gp44));
    get_status(dr);
    int blocks = ((uint8_t)last_status[27] << 16) | ((uint8_t)last_status[28] << 8) | (uint8_t)last_status[29];
    printf("%s: G-P 44 type %d, byte 1 %d, size %d blocks\n", testname, (uint8_t)last_status[0],
           (uint8_t)last_status[1], blocks);
    REQUIRE(last_status_size == 31);
    REQUIRE((last_status[0] == 2) && (last_status[1] == 0) && (last_status[2] == 44));
    REQUIRE(blocks == 342);

    // A directory partition reports its volume: the 16 MB FAT file this suite runs on,
    // which is no more than 32768 blocks and no less than what is free on it.
    const uint8_t gp40[4] = { 'G', '-', 'P', 40 };
    send_command_data(dr, gp40, sizeof(gp40));
    get_status(dr);
    blocks = ((uint8_t)last_status[27] << 16) | ((uint8_t)last_status[28] << 8) | (uint8_t)last_status[29];
    Path fat_path("/Fat/");
    uint32_t free_clusters = 0, cluster_size = 0;
    REQUIRE(fm->get_free(&fat_path, free_clusters, cluster_size) == FR_OK);
    uint32_t free_blocks = (uint32_t)(((uint64_t)free_clusters * cluster_size) / 512);
    printf("%s: G-P 40 size %d blocks, %u blocks free on the volume\n", testname, blocks, free_blocks);
    REQUIRE((blocks <= 32768) && (blocks >= (int)free_blocks) && (blocks > 0));
}

// SI-072: a file named with a disk image or cartridge extension is stored under exactly
// that name, without a type extension, so a PC sees GAME.D81 and not GAME.D81.prg.
static void s11_si072_raw_names(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI072-RawNames";
    const char *path = s11_partition(fm, dr, "si072");
    static const char *names[] = { "X.D81", "Y.D64", "Z.DNP", "GAME.CRT", "GAME.TCRT" };
    char host[80];
    FileInfo info(16);
    char open_name[40];
    for (int i = 0; i < 5; i++) {
        snprintf(open_name, sizeof(open_name), "%s,P,W", names[i]);
        expect_iec_write_ok(testname, dr, 2, open_name, "raw");
        snprintf(host, sizeof(host), "%s/%s", path, names[i]);
        if (fm->fstat(host, info) != FR_OK) {
            printf("%s: expected host file '%s'\n", testname, host);
        }
        REQUIRE(fm->fstat(host, info) == FR_OK);
        snprintf(host, sizeof(host), "%s/%s.prg", path, names[i]);
        if (fm->fstat(host, info) == FR_OK) {
            printf("%s: host file '%s' should not exist\n", testname, host);
        }
        REQUIRE(fm->fstat(host, info) != FR_OK);
    }
    expect_iec_file(testname, dr, 0, "X.D81", "raw");
    // Any other extension still gets the type.
    expect_iec_write_ok(testname, dr, 2, "NOTES.TXT,S,W", "typed");
    snprintf(host, sizeof(host), "%s/NOTES.TXT.seq", path);
    REQUIRE(fm->fstat(host, info) == FR_OK);
}

// SI-074: a rename refuses a name that exists with any type (63) and an empty name (34),
// and moves a file into another directory of the partition, as it did before.
// Whether a directory listing carries a run of bytes, for a case that has to assert
// that an entry is gone as well as that one is there.
static bool listing_holds(const uint8_t *listing, int got, const char *text)
{
    int len = strlen(text);
    for (int i = 0; i <= got - len; i++) {
        if (memcmp(listing + i, text, len) == 0) {
            return true;
        }
    }
    return false;
}

static void s11_si074_rename_checks(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI074-RenameChecks";
    s11_partition(fm, dr, "si074");
    expect_command_ok(testname, dr, "MD:SUB\r");
    expect_iec_write_ok(testname, dr, 1, "OLD", "old");
    expect_iec_write_ok(testname, dr, 2, "NEW,S,W", "new");
    expect_command_ok(testname, dr, "R/SUB/:MOVED=OLD\r");
    expect_iec_file(testname, dr, 0, "/SUB/:MOVED", "old");
    expect_command_ok(testname, dr, "R:OLD=/SUB/:MOVED\r");
    expect_iec_file(testname, dr, 0, "OLD", "old");
    expect_command_response(testname, dr, "R:NEW=OLD\r", "63,FILE EXISTS,00,00\r");
    expect_iec_file(testname, dr, 0, "OLD", "old");
    expect_command_response(testname, dr, "R:=OLD\r", "34,SYNTAX ERROR,00,00\r");
    expect_command_ok(testname, dr, "R:FRESH=OLD\r");
    expect_iec_file(testname, dr, 0, "FRESH", "old");

    // A subdirectory is renamed under its own name, as HD 9-26 "Renaming Files and
    // Subdirectories" describes and as SD parse_rename() does: it matches an entry of
    // any type, so a directory entry is found.
    expect_command_ok(testname, dr, "MD:DIRA\r");
    expect_command_ok(testname, dr, "R:DIRB=DIRA\r");
    uint8_t listing[4096];
    int got = read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    REQUIRE(listing_holds(listing, got, "\"DIRB\""));
    REQUIRE(!listing_holds(listing, got, "\"DIRA\""));
    // The renamed directory is still a directory, and can be entered under its new name.
    REQUIRE(listing_holds(listing, got, "DIR"));
    expect_command_ok(testname, dr, "CD:DIRB\r");
    expect_command_ok(testname, dr, "CD//\r");
    // The name a directory already has is refused for a file and for another directory.
    expect_command_response(testname, dr, "R:DIRB=FRESH\r", "63,FILE EXISTS,00,00\r");
    expect_command_ok(testname, dr, "MD:DIRC\r");
    expect_command_response(testname, dr, "R:DIRB=DIRC\r", "63,FILE EXISTS,00,00\r");
    // A name that belongs to nothing still answers 62.
    expect_command_response(testname, dr, "R:DIRD=NOSUCHDIR\r", "62,FILE NOT FOUND,00,00\r");
}

// SI-083: P on a file opened for writing moves past the end, and the next write extends
// the file. This is JiffyDOS's MD81, as the reporter's #877 excerpt shows it: open
// FOO.D81,P,W, position to 819199, write one byte, close.
static void s11_si083_seek_write(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI083-SeekWrite";
    const char *path = s11_partition(fm, dr, "si083");
    const uint8_t chan = 2;
    open_file(dr, chan, "FOO.D81,P,W");
    get_status(dr);
    expect_status_ok(testname, "FOO.D81,P,W");
    const uint8_t md81[6] = { 'P', (uint8_t)(96 + chan), 0xFF, 0x7F, 0x0C, 0x00 };
    expect_command_data_response(testname, dr, md81, sizeof(md81), "00, OK,00,00\r");
    const uint8_t last = 0x42;
    send_channel_data(dr, chan, &last, 1);
    close_file(dr, chan);
    expect_status_ok(testname, "close FOO.D81");
    char host[80];
    snprintf(host, sizeof(host), "%s/FOO.D81", path);
    FileInfo info(16);
    REQUIRE(fm->fstat(host, info) == FR_OK);
    printf("%s: FOO.D81 is %u bytes\n", testname, (unsigned)info.size);
    REQUIRE(info.size == 819200);
    expect_command_response(testname, dr, "S:FOO.D81\r", "01, FILES SCRATCHED,01,00\r");

    // What was written before the position is kept, and the write lands where P put it.
    open_file(dr, chan, "HOLES,S,W");
    get_status(dr);
    send_channel_data(dr, chan, (const uint8_t *)"HEAD", 4);
    const uint8_t p1000[6] = { 'P', (uint8_t)(96 + chan), 0xE8, 0x03, 0x00, 0x00 };
    expect_command_data_response(testname, dr, p1000, sizeof(p1000), "00, OK,00,00\r");
    send_channel_data(dr, chan, (const uint8_t *)"TAIL", 4);
    close_file(dr, chan);
    uint8_t content[1100];
    open_file(dr, 3, "HOLES,S,R");
    get_status(dr);
    int got = read_file(dr, 3, content, sizeof(content));
    close_file(dr, 3);
    printf("%s: HOLES is %d bytes\n", testname, got);
    REQUIRE(got == 1004);
    REQUIRE(memcmp(content, "HEAD", 4) == 0);
    REQUIRE(memcmp(content + 1000, "TAIL", 4) == 0);
}

// SI-083 and SI-036: inside a disk image a file cannot be positioned past its end, and
// the answer is 72, which CBM DOS names, not the Ultimate's 69.
static void s11_si083_seek_write_image(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI083-SeekWriteImage";
    s11_partition(fm, dr, "si083i");
    create_formatted_image(fm, "/Fat/s11_si083.d81", "SEEK", 3200, e_image_d81);
    dr->add_partition(45, "/Fat/s11_si083.d81", "SEEKIMAGE");
    const uint8_t chan = 2;
    open_file(dr, chan, "45:FOO,P,W");
    get_status(dr);
    expect_status_ok(testname, "45:FOO,P,W");
    const uint8_t md81[6] = { 'P', (uint8_t)(96 + chan), 0xFF, 0x7F, 0x0C, 0x00 };
    expect_command_data_response(testname, dr, md81, sizeof(md81), "72,DISK FULL,00,00\r");
    close_file(dr, chan);
}

static uint32_t s11_host_size(FileManager *fm, const char *dir, const char *name)
{
    char host[96];
    snprintf(host, sizeof(host), "%s/%s", dir, name);
    FileInfo info(16);
    if (fm->fstat(host, info) != FR_OK) {
        return 0;
    }
    return info.size;
}

// SI-071: N:name[,id] creates a disk image, or formats one, the way sd2iec does, because
// this drive has no medium of its own to format. The extension picks the format; no
// extension means .D64, and then an existing file is not overwritten.
// SI-052, SI-071: N creates and formats the four image kinds, and formats a mounted
// image in place.
static void s11_si071_format(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI071-Format";
    const char *path = s11_partition(fm, dr, "si071");
    static const struct { const char *cmd; const char *file; uint32_t size; } images[] = {
        { "N:ONE.D64,AA\r",  "ONE.D64", 174848 },
        { "N:TWO.D71,AB\r",  "TWO.D71", 349696 },
        { "N:THREE.D81,AC\r", "THREE.D81", 819200 },
    };
    for (int i = 0; i < 3; i++) {
        expect_command_ok(testname, dr, images[i].cmd);
        uint32_t size = s11_host_size(fm, path, images[i].file);
        printf("%s: %s is %u bytes, expected %u\n", testname, images[i].file, size, images[i].size);
        REQUIRE(size == images[i].size);
    }
    // A created image mounts, lists with its label, and takes files.
    expect_command_ok(testname, dr, "CD:ONE.D64\r");
    expect_directory_contains(testname, dr, "$", "\"ONE ");
    expect_iec_write_ok(testname, dr, 2, "INSIDE,S,W", "in the image");
    expect_iec_file(testname, dr, 2, "INSIDE,S,R", "in the image");
    expect_command_ok(testname, dr, "CD:_\r");
    // Given explicitly, an existing image's extension means format it again.
    expect_command_ok(testname, dr, "N:ONE.D64,ZZ\r");
    expect_command_ok(testname, dr, "CD:ONE.D64\r");
    expect_iec_file_missing(testname, dr, 2, "INSIDE,S,R");
    expect_command_ok(testname, dr, "CD:_\r");

    // A DNP takes a three digit track count as its id and is created, not formatted.
    expect_command_ok(testname, dr, "N:FOUR.DNP,002\r");
    REQUIRE(s11_host_size(fm, path, "FOUR.DNP") == 2 * 65536);
    expect_command_response(testname, dr, "N:FOUR.DNP,002\r", "63,FILE EXISTS,00,00\r");

    // No extension: .D64 is added, and an existing one is left alone.
    expect_command_ok(testname, dr, "N:PLAIN,PL\r");
    REQUIRE(s11_host_size(fm, path, "PLAIN.D64") == 174848);
    expect_command_response(testname, dr, "N:PLAIN,PL\r", "63,FILE EXISTS,00,00\r");

    // A new image needs an id, of two or three characters; the name needs a colon.
    expect_command_response(testname, dr, "N:NOID.D64\r", "30,SYNTAX ERROR,00,00\r");
    REQUIRE(s11_host_size(fm, path, "NOID.D64") == 0);
    expect_command_response(testname, dr, "N:BADID.D64,A\r", "30,SYNTAX ERROR,00,00\r");
    expect_command_response(testname, dr, "N:BADDNP.DNP,12\r", "30,SYNTAX ERROR,00,00\r");
    expect_command_response(testname, dr, "NNOCOLON,AA\r", "34,SYNTAX ERROR,00,00\r");
    expect_command_response(testname, dr, "N:,AA\r", "34,SYNTAX ERROR,00,00\r");
    // Room for the image is checked before anything is created.
    expect_command_response(testname, dr, "N:HUGE.DNP,255\r", "72,DISK FULL,00,00\r");
    REQUIRE(s11_host_size(fm, path, "HUGE.DNP") == 0);

    // A full sixteen character label keeps its extension.
    expect_command_ok(testname, dr, "N:ABCDEFGHIJKLMNOP.D81,AB\r");
    REQUIRE(s11_host_size(fm, path, "ABCDEFGHIJKLMNOP.D81") == 819200);
    // Inside a disk image N formats that image rather than creating an image in it.
    expect_command_ok(testname, dr, "CD:ONE.D64\r");
    expect_iec_write_ok(testname, dr, 2, "GONE,S,W", "erased by the format");
    expect_command_ok(testname, dr, "N:WORK,01\r");
    expect_iec_file_missing(testname, dr, 2, "WORK.D64,S,R");
    expect_iec_file_missing(testname, dr, 2, "GONE,S,R");
    expect_directory_contains(testname, dr, "$", "\"WORK ");
    // The image keeps its size, and takes files again.
    expect_iec_write_ok(testname, dr, 2, "AFTER,S,W", "written after the format");
    expect_iec_file(testname, dr, 2, "AFTER,S,R", "written after the format");
    expect_command_ok(testname, dr, "CD:_\r");
    REQUIRE(s11_host_size(fm, path, "ONE.D64") == 174848);
    // A file open on the image is not formatted out from under its channel.
    expect_command_ok(testname, dr, "CD:ONE.D64\r");
    open_file(dr, 3, "AFTER,S,R");
    get_status(dr);
    expect_command_response(testname, dr, "N:BUSY,02\r", "60,WRITE FILE OPEN,00,00\r");
    close_file(dr, 3);
    expect_command_ok(testname, dr, "N:BUSY,02\r");
    expect_command_ok(testname, dr, "CD:_\r");

    expect_command_response(testname, dr, "S:*\r", "01, FILES SCRATCHED,06,00\r");
}

// SI-147 and SI-148: a shifted space ($A0) is a legal byte inside a name and maps to
// {A0}; a trailing run is padding and is dropped; a file an older build named with the
// padding kept is still found by its CBM name; and a listing shows the name up to its
// terminator, not up to the first $A0. testdrive is also built with -funsigned-char
// (target/pc/linux/iecdrive_unsigned), because the rule depended on char signedness.
static void s11_si147_shifted_space(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI147-ShiftedSpace";
    const char *path = s11_partition(fm, dr, "si147");
    FileInfo info(16);
    char host[80];

    expect_iec_write_ok(testname, dr, 2, "AB\xA0,S,W", "padded");
    snprintf(host, sizeof(host), "%s/AB.seq", path);
    if (fm->fstat(host, info) != FR_OK) {
        printf("%s: expected host file '%s'\n", testname, host);
    }
    REQUIRE(fm->fstat(host, info) == FR_OK);
    expect_iec_file(testname, dr, 2, "AB\xA0,S,R", "padded");
    expect_iec_file(testname, dr, 2, "AB,S,R", "padded");

    expect_iec_write_ok(testname, dr, 2, "A\xA0" "B,S,W", "inside");
    snprintf(host, sizeof(host), "%s/A{A0}B.seq", path);
    if (fm->fstat(host, info) != FR_OK) {
        printf("%s: expected host file '%s'\n", testname, host);
    }
    REQUIRE(fm->fstat(host, info) == FR_OK);
    expect_directory_contains(testname, dr, "$", "\"A\xA0" "B\"");

    // A host name an earlier build produced with the padding escaped.
    uint32_t tr;
    REQUIRE(fm->save_file(true, path, "LEGACY{A0}.prg", (const uint8_t *)"old name", 8, &tr) == FR_OK);
    expect_iec_file(testname, dr, 0, "LEGACY\xA0", "old name");
}

// SI-142: a host name escapes * and ?, so a pattern is matched against the CBM names
// by the drive and never by the host file system: a scratch, an RD and an open by
// pattern still find what they name, on a host directory and inside an image.
static void s11_si142_escaped_wildcards(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI142-EscapedWildcards";
    s11_partition(fm, dr, "si142");
    expect_iec_write_ok(testname, dr, 2, "FILE1,S,W", "one");
    expect_iec_write_ok(testname, dr, 2, "FILE2,S,W", "two");
    expect_iec_write_ok(testname, dr, 2, "OTHER,S,W", "other");
    expect_iec_file(testname, dr, 2, "FIL*,S,R", "one");
    expect_command_response(testname, dr, "S:FIL*\r", "01, FILES SCRATCHED,02,00\r");
    expect_iec_file(testname, dr, 2, "OTHER,S,R", "other");
    expect_command_ok(testname, dr, "MD:ABCDIR\r");
    expect_command_ok(testname, dr, "RD:ABC*\r");
    expect_directory_contains(testname, dr, "$", "\"OTHER\"");
    create_formatted_image(fm, "/Fat/s11_si142.d64", "PATTERNS", 683, e_image_d64);
    expect_command_ok(testname, dr, "CD//\r");
    dr->add_partition(46, "/Fat/s11_si142.d64", "PATTERNS");
    expect_iec_write_ok(testname, dr, 2, "46:GAME1,P,W", "g1");
    expect_iec_write_ok(testname, dr, 2, "46:GAME2,P,W", "g2");
    expect_command_response(testname, dr, "S46:GAME?\r", "01, FILES SCRATCHED,02,00\r");
}

// The type field of the listing line for `name`, and the character behind it, which is
// the lock marker.
static void s11_listing_type(IecDrive *dr, const char *testname, const char *dir, const char *name,
                             char *type, bool *present)
{
    uint8_t listing[4096];
    int got = read_directory_stream(testname, dr, dir, listing, sizeof(listing));
    const uint8_t *line = s11_listing_line(listing, got, name);
    *present = (line != NULL);
    type[0] = 0;
    if (line) {
        int blocks = line[2] | (line[3] << 8);
        int digits = (blocks >= 100) ? 3 : (blocks >= 10) ? 2 : 1;
        memcpy(type, line + 27 - digits, 4);
        type[4] = 0;
    }
}

// SI-076 and SI-132: L toggles the lock of a file or a directory. A locked file lists
// with < behind its type and is not scratched; a locked directory is not removed.
static void s11_si076_lock(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI076-Lock";
    s11_partition(fm, dr, "si076");
    char type[8];
    bool present;
    expect_iec_write_ok(testname, dr, 1, "LOCKME", "keep");
    expect_command_ok(testname, dr, "L:LOCKME\r");
    s11_listing_type(dr, testname, "$", "LOCKME", type, &present);
    printf("%s: locked file lists as '%s'\n", testname, type);
    REQUIRE(present && !strcmp(type, "PRG<"));
    expect_command_response(testname, dr, "S:LOCKME\r", "01, FILES SCRATCHED,00,00\r");
    expect_iec_file(testname, dr, 0, "LOCKME", "keep");
    expect_command_ok(testname, dr, "L:LOCKME\r");
    s11_listing_type(dr, testname, "$", "LOCKME", type, &present);
    REQUIRE(present && !strcmp(type, "PRG "));
    expect_command_response(testname, dr, "S:LOCKME\r", "01, FILES SCRATCHED,01,00\r");

    expect_command_ok(testname, dr, "MD:LOCKDIR\r");
    expect_command_ok(testname, dr, "L:LOCKDIR\r");
    expect_command_status_prefix(testname, dr, "RD:LOCKDIR\r", "63,");
    expect_command_ok(testname, dr, "L:LOCKDIR\r");
    expect_command_ok(testname, dr, "RD:LOCKDIR\r");
    expect_command_response(testname, dr, "L:NOSUCH\r", "62,FILE NOT FOUND,00,00\r");

    // Inside a disk image the lock is the CBM lock bit.
    create_formatted_image(fm, "/Fat/s11_si076.d64", "LOCKS", 683, e_image_d64);
    dr->add_partition(47, "/Fat/s11_si076.d64", "LOCKS");
    expect_iec_write_ok(testname, dr, 1, "47:INIMAGE", "img");
    expect_command_ok(testname, dr, "L47:INIMAGE\r");
    s11_listing_type(dr, testname, "$47", "INIMAGE", type, &present);
    printf("%s: locked file in a D64 lists as '%s'\n", testname, type);
    REQUIRE(present && !strcmp(type, "PRG<"));
    expect_command_response(testname, dr, "S47:INIMAGE\r", "01, FILES SCRATCHED,00,00\r");
}

// SI-102: while the software write protect is set, every command and every open that
// would change a medium answers 26 and changes nothing, and everything that only reads
// still works. One check per gate the drive guards, so a gate that is left out is a
// failure here rather than a hole nobody notices.
static void s11_si102_write_protect(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI102-WriteProtect";
    const char *protect = "26,WRITE PROTECT ON,00,00\r";
    s11_partition(fm, dr, "si102");
    expect_iec_write_ok(testname, dr, 1, "KEEP", "keep");
    expect_command_ok(testname, dr, "MD:SUB\r");
    expect_rel_open(testname, dr, 2, "RECORDS", 8);
    expect_rel_position_status(testname, dr, 2, 1, 1, "50,RECORD NOT PRESENT,00,00\r");
    expect_rel_write(testname, dr, 2, (const uint8_t *)"FIRSTREC", 8);
    close_file(dr, 2);
    create_formatted_image(fm, "/Fat/s11_si102.d64", "PROTECT", 683, e_image_d64);
    dr->add_partition(44, "/Fat/s11_si102.d64", "PROTECT");

    expect_command_ok(testname, dr, "W-1\r");

    // The commands that change a medium.
    expect_command_response(testname, dr, "MD:NEWDIR\r", protect);
    expect_command_response(testname, dr, "RD:SUB\r", protect);
    expect_command_response(testname, dr, "C:COPY=KEEP\r", protect);
    expect_command_response(testname, dr, "N:FRESH.D64\r", protect);
    expect_command_response(testname, dr, "R:OTHER=KEEP\r", protect);
    expect_command_response(testname, dr, "S:KEEP\r", protect);
    expect_command_response(testname, dr, "R-H:NEWHEAD\r", protect);
    expect_command_response(testname, dr, "L:KEEP\r", protect);
    expect_command_response(testname, dr, "EL:KEEP\r", protect);
    expect_command_response(testname, dr, "EU:KEEP\r", protect);
    expect_command_response(testname, dr, "EHKEEP\r", protect);
    expect_command_response(testname, dr, "A:R=KEEP\r", protect);
    expect_command_response(testname, dr, "R-P:OTHER=PROTECT\r", protect);

    // The block commands that write, on the image partition.
    expect_command_response(testname, dr, "CP44\r", "02,PARTITION SELECTED,44,00\r");
    open_buffer_channel(testname, dr, 3);
    expect_command_response(testname, dr, "U2:3,0,1,0\r", protect);
    expect_command_response(testname, dr, "B-W:3,0,1,0\r", protect);
    expect_command_response(testname, dr, "B-A:0,1,0\r", protect);
    expect_command_response(testname, dr, "B-F:0,1,0\r", protect);
    // Reading a block still works.
    expect_command_ok(testname, dr, "U1:3,0,1,0\r");
    close_file(dr, 3);
    expect_command_response(testname, dr, "CP40\r", "02,PARTITION SELECTED,40,00\r");

    // The opens that would write, and the record write of a relative file, which is
    // opened for reading and writing whatever the command asks for.
    expect_iec_open_status_prefix(testname, dr, 1, "NEWFILE", "26,");
    expect_iec_open_status_prefix(testname, dr, 1, "@KEEP", "26,");
    expect_iec_open_status_prefix(testname, dr, 2, "KEEP,S,A", "26,");
    // The record that is there is read; the one past the end is not created, and a
    // write to either is refused.
    expect_rel_open(testname, dr, 2, "RECORDS", 8);
    expect_rel_position_status(testname, dr, 2, 1, 1, "00, OK,00,00\r");
    expect_rel_read(testname, dr, 2, (const uint8_t *)"FIRSTREC", 8);
    expect_rel_position_status(testname, dr, 2, 9, 1, "50,RECORD NOT PRESENT,00,00\r");
    expect_rel_position_status(testname, dr, 2, 1, 1, "00, OK,00,00\r");
    send_channel_data(dr, 2, (const uint8_t *)"12345678", 8);
    get_status(dr);
    expect_current_status(testname, "REL write while protected", protect);
    // The channel stays usable, and the record still holds what it held.
    expect_rel_position_status(testname, dr, 2, 1, 1, "00, OK,00,00\r");
    expect_rel_read(testname, dr, 2, (const uint8_t *)"FIRSTREC", 8);
    close_file(dr, 2);

    // Nothing was changed, and reading is unaffected.
    expect_iec_file(testname, dr, 0, "KEEP", "keep");
    char type[8];
    bool present;
    s11_listing_type(dr, testname, "$", "KEEP", type, &present);
    REQUIRE(present && !strcmp(type, "PRG "));
    s11_listing_type(dr, testname, "$", "SUB", type, &present);
    REQUIRE(present);
    expect_command_ok(testname, dr, "CD//SUB\r");
    expect_command_ok(testname, dr, "CD//\r");

    // W-0 gives the medium back.
    expect_command_ok(testname, dr, "W-0\r");
    expect_command_ok(testname, dr, "MD:NEWDIR\r");
    expect_command_ok(testname, dr, "RD:NEWDIR\r");
    expect_command_response(testname, dr, "W-2\r", "30,SYNTAX ERROR,00,00\r");
    dr->get_file_system()->RemovePartition(44);
}

// SI-077: the sd2iec spellings of the attribute commands. EL and EU set and clear the
// lock that L turns over (SI-076), EH turns the hidden flag over, and A names every
// attribute an entry is to carry.
static void s11_si077_attribute_commands(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI077-AttributeCommands";
    s11_partition(fm, dr, "si077");
    char type[8];
    bool present;
    expect_iec_write_ok(testname, dr, 1, "ONE", "1");
    expect_iec_write_ok(testname, dr, 1, "TWO", "2");

    // EL locks every entry each name matches, and a listing marks a locked entry.
    expect_command_ok(testname, dr, "EL:ONE,TWO\r");
    s11_listing_type(dr, testname, "$", "ONE", type, &present);
    REQUIRE(present && !strcmp(type, "PRG<"));
    s11_listing_type(dr, testname, "$", "TWO", type, &present);
    REQUIRE(present && !strcmp(type, "PRG<"));
    expect_command_response(testname, dr, "S:ONE\r", "01, FILES SCRATCHED,00,00\r");

    // EU clears it again, on a pattern this time.
    expect_command_ok(testname, dr, "EU:*\r");
    s11_listing_type(dr, testname, "$", "ONE", type, &present);
    REQUIRE(present && !strcmp(type, "PRG "));
    expect_command_response(testname, dr, "S:ONE\r", "01, FILES SCRATCHED,01,00\r");

    // A sets exactly the attributes it names, so R alone locks and clears the rest.
    expect_command_ok(testname, dr, "A:R=TWO\r");
    s11_listing_type(dr, testname, "$", "TWO", type, &present);
    REQUIRE(present && !strcmp(type, "PRG<"));
    expect_command_ok(testname, dr, "A:=TWO\r");
    s11_listing_type(dr, testname, "$", "TWO", type, &present);
    REQUIRE(present && !strcmp(type, "PRG "));
    // H hides the entry and clears the lock in the same command.
    expect_command_ok(testname, dr, "A:H=TWO\r");
    uint8_t listing[4096];
    int got = read_directory_stream(testname, dr, "$:*", listing, sizeof(listing));
    REQUIRE(s11_listing_line(listing, got, "TWO") == NULL);
    got = read_directory_stream(testname, dr, "$:*=H", listing, sizeof(listing));
    REQUIRE(s11_listing_line(listing, got, "TWO") != NULL);
    expect_command_ok(testname, dr, "A:=TWO\r");

    // A name that matches nothing, which is also what the sd2iec form that write
    // protects a whole image answers, because `$` matches no entry (SI-077).
    expect_command_response(testname, dr, "EL:$\r", "62,FILE NOT FOUND,00,00\r");
    expect_command_response(testname, dr, "EU:$\r", "62,FILE NOT FOUND,00,00\r");
    expect_command_response(testname, dr, "EL:NOSUCH\r", "62,FILE NOT FOUND,00,00\r");
    expect_command_response(testname, dr, "EHNOSUCH\r", "62,FILE NOT FOUND,00,00\r");

    // Inside a CBM image the lock is the type byte's bit 6 and there is no hidden flag,
    // so EL and EU work and EH answers 30.
    create_formatted_image(fm, "/Fat/s11_si077.d64", "ATTRS", 683, e_image_d64);
    dr->add_partition(43, "/Fat/s11_si077.d64", "ATTRS");
    expect_iec_write_ok(testname, dr, 1, "43:INIMAGE", "i");
    expect_command_ok(testname, dr, "EL43:INIMAGE\r");
    s11_listing_type(dr, testname, "$43", "INIMAGE", type, &present);
    REQUIRE(present && !strcmp(type, "PRG<"));
    // A colon straight after the partition number is the header form, so the hidden
    // flag of an entry in partition 43 is asked for with a path.
    expect_command_response(testname, dr, "EH43/:INIMAGE\r", "30,SYNTAX ERROR,00,00\r");
    expect_command_ok(testname, dr, "EU43:INIMAGE\r");
    s11_listing_type(dr, testname, "$43", "INIMAGE", type, &present);
    REQUIRE(present && !strcmp(type, "PRG "));
    dr->get_file_system()->RemovePartition(43);
}

// SI-051: R-P:new=old renames the partition the old name belongs to, and names no
// partition when nothing carries the old name.
static void s11_si051_rename_partition(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI051-RenamePartition";
    s11_partition(fm, dr, "si051");
    dr->add_partition(41, "/Temp", "TORENAME");
    uint8_t listing[8192];
    int got;

    expect_command_ok(testname, dr, "R-P:RENAMED=TORENAME\r");
    got = read_directory_stream(testname, dr, "$=P", listing, sizeof(listing));
    REQUIRE(memmem(listing, got, "RENAMED", 7) != NULL);
    REQUIRE(memmem(listing, got, "TORENAME", 8) == NULL);

    // The name a partition carries is also what G-P answers with, in bytes 3 to 18.
    expect_command_data_response(testname, dr, (const uint8_t *)"CP41\r", 5,
                                 "02,PARTITION SELECTED,41,00\r");
    send_command(dr, "G-P");
    REQUIRE(memcmp(last_status + 3, "RENAMED", 7) == 0);

    expect_command_response(testname, dr, "R-P:X=NOSUCHPART\r",
                            "77,SELECTED PARTITION ILLEGAL,00,00\r");
    expect_command_response(testname, dr, "R-P:=TORENAME\r", "34,SYNTAX ERROR,00,00\r");
    expect_command_response(testname, dr, "R-P:RENAMED\r", "30,SYNTAX ERROR,00,00\r");
    // Put the partition back, so a later case that reads the partition directory is not
    // looking at a name this one left behind.
    expect_command_response(testname, dr, "CP40\r", "02,PARTITION SELECTED,40,00\r");
    dr->get_file_system()->RemovePartition(41);
}

// SI-064: R-H renames the header a listing of that directory shows, which is the
// directory's own name on the host file system (SI-065), the partition's name at the
// root of a partition, and the disk name inside a CBM image.
static void s11_si064_rename_header(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI064-RenameHeader";
    s11_partition(fm, dr, "si064");
    uint8_t listing[8192];
    char header[17];
    header[16] = 0;

    // A subdirectory: its header is its name, so the directory answers to the new one.
    expect_command_ok(testname, dr, "MD:GAMES\r");
    expect_command_ok(testname, dr, "R-H/GAMES/:ARCADE\r");
    read_directory_stream(testname, dr, "$/ARCADE/", listing, sizeof(listing));
    memcpy(header, listing + 8, 16);
    printf("%s: header of $/ARCADE/ is '%s'\n", testname, header);
    REQUIRE(memcmp(listing + 8, "ARCADE          ", 16) == 0);
    expect_command_ok(testname, dr, "CD//ARCADE\r");
    expect_command_ok(testname, dr, "R-H:ACTION\r"); // no path: the current directory
    read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    REQUIRE(memcmp(listing + 8, "ACTION          ", 16) == 0);
    expect_command_ok(testname, dr, "CD//\r");

    // The root of a partition, whose header is the partition name.
    expect_command_ok(testname, dr, "R-H:HOSTPART\r");
    read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    memcpy(header, listing + 8, 16);
    printf("%s: header at the partition root is '%s'\n", testname, header);
    REQUIRE(memcmp(listing + 8, "HOSTPART        ", 16) == 0);

    // Inside a CBM image the header is the disk name, and the id stays as it is unless
    // the command carries one.
    create_formatted_image(fm, "/Fat/s11_si064.d64", "OLDNAME", 683, e_image_d64);
    dr->add_partition(42, "/Fat/s11_si064.d64", "IMAGEPART");
    read_directory_stream(testname, dr, "$42", listing, sizeof(listing));
    char id[6];
    memcpy(id, listing + 8 + 18, 5);
    id[5] = 0;
    expect_command_ok(testname, dr, "R-H42:NEWDISK\r");
    read_directory_stream(testname, dr, "$42", listing, sizeof(listing));
    memcpy(header, listing + 8, 16);
    printf("%s: header of $42 is '%s'\n", testname, header);
    REQUIRE(memcmp(listing + 8, "NEWDISK         ", 16) == 0);
    REQUIRE(memcmp(listing + 8 + 18, id, 5) == 0);
    expect_command_ok(testname, dr, "R-H42:WITHID,QQ\r");
    read_directory_stream(testname, dr, "$42", listing, sizeof(listing));
    memcpy(header, listing + 8, 16);
    printf("%s: header of $42 with an id is '%s'\n", testname, header);
    REQUIRE(memcmp(listing + 8, "WITHID          ", 16) == 0);
    REQUIRE(memcmp(listing + 8 + 18, "QQ", 2) == 0);

    // A name that is empty or carries a wildcard is no name to give a header.
    expect_command_response(testname, dr, "R-H:\r", "34,SYNTAX ERROR,00,00\r");
    expect_command_response(testname, dr, "R-H:NEW*\r", "33,SYNTAX ERROR,00,00\r");
    // A path that is not there.
    expect_command_response(testname, dr, "R-H/NOSUCH/:NAME\r", "71,DIRECTORY ERROR,40,00\r");

    // A native image carries a header block per subdirectory, which is the header a
    // listing of that subdirectory shows, so R-H there writes that block and not the
    // volume name of the root.
    create_formatted_image(fm, "/Fat/s11_si064.dnp", "NATIVE", 4 * 256, e_image_dnp);
    dr->add_partition(43, "/Fat/s11_si064.dnp", "NATIVEPART");
    expect_command_ok(testname, dr, "MD43:TOOLS\r");
    expect_command_ok(testname, dr, "R-H43//TOOLS/:UTILITIES\r");
    read_directory_stream(testname, dr, "$43//TOOLS/", listing, sizeof(listing));
    memcpy(header, listing + 8, 16);
    printf("%s: header of $43//TOOLS/ is '%s'\n", testname, header);
    REQUIRE(memcmp(listing + 8, "UTILITIES       ", 16) == 0);
    // The root of the image keeps its own name, and the subdirectory keeps the name its
    // parent holds, because a native image has a header separate from that name.
    read_directory_stream(testname, dr, "$43//", listing, sizeof(listing));
    memcpy(header, listing + 8, 16);
    printf("%s: header of $43// is '%s'\n", testname, header);
    REQUIRE(memcmp(listing + 8, "NATIVE          ", 16) == 0);
    expect_command_ok(testname, dr, "CD43//TOOLS\r");
    expect_command_ok(testname, dr, "CD43//\r");
    dr->get_file_system()->RemovePartition(43);
    dr->get_file_system()->RemovePartition(42);
}

// A 1541 image on partition `part`, selected, with a buffer channel open on `chan`.
static void s11_block_partition(FileManager *fm, IecDrive *dr, const char *testname, int part,
                                const char *image, uint8_t chan)
{
    char cmd[16];
    create_formatted_image(fm, image, "BLOCKS", 683, e_image_d64);
    dr->add_partition(part, image, "BLOCKS");
    snprintf(cmd, sizeof(cmd), "CP%d\r", part);
    expect_command_status_prefix(testname, dr, cmd, "02,PARTITION SELECTED");
    open_buffer_channel(testname, dr, chan);
}

// SI-090: a standard buffer starts with its pointer at byte 1, so what is written right
// after the open lands from byte 1 on.
static void s11_si090_buffer_pointer(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI090-BufferPointer";
    s11_block_partition(fm, dr, testname, 49, "/Fat/s11_si090.d64", 2);
    send_channel_data(dr, 2, (const uint8_t *)"ABC", 3);
    expect_command_ok(testname, dr, "U2:2,0,1,0\r");
    expect_command_ok(testname, dr, "U1:2,0,1,0\r");
    uint8_t sector[256];
    read_buffer_channel(testname, dr, 2, sector, sizeof(sector));
    printf("%s: bytes 1 to 3 are %02X %02X %02X\n", testname, sector[1], sector[2], sector[3]);
    REQUIRE(memcmp(sector + 1, "ABC", 3) == 0);
    close_file(dr, 2);

    // "##1" is the same buffer with its pointer at byte 0, so what is written after the
    // open lands from byte 0 on. A chain of more than one is refused.
    open_file(dr, 2, "##1");
    get_status(dr);
    expect_current_status(testname, "##1", "00, OK,00,00\r");
    send_channel_data(dr, 2, (const uint8_t *)"XYZ", 3);
    expect_command_ok(testname, dr, "U2:2,0,2,0\r");
    expect_command_ok(testname, dr, "U1:2,0,2,0\r");
    read_buffer_channel(testname, dr, 2, sector, sizeof(sector));
    printf("%s: bytes 0 to 2 are %02X %02X %02X\n", testname, sector[0], sector[1], sector[2]);
    REQUIRE(memcmp(sector, "XYZ", 3) == 0);
    // A position a high byte puts past the end of a 256 byte buffer names no byte.
    expect_command_response(testname, dr, "B-P 2 4 1\r", "30,SYNTAX ERROR,00,00\r");
    expect_command_ok(testname, dr, "B-P 2 4 0\r");
    close_file(dr, 2);

    expect_iec_open_status_prefix(testname, dr, 2, "##2", "70,");
    expect_iec_open_status_prefix(testname, dr, 2, "##9", "70,");
    // Anything else after the # is the standard buffer.
    open_file(dr, 2, "##");
    get_status(dr);
    expect_current_status(testname, "##", "00, OK,00,00\r");
    close_file(dr, 2);
}

// SI-093 and SI-013: a direct access channel keeps the partition that was current when
// it was opened, and the partition number in a block command is ignored.
static void s11_si093_bound_partition(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI093-BoundPartition";
    s11_partition(fm, dr, "si093");
    s11_block_partition(fm, dr, testname, 50, "/Fat/s11_si093.d64", 2);
    expect_command_response(testname, dr, "CP40\r", "02,PARTITION SELECTED,40,00\r");
    expect_command_ok(testname, dr, "U1:2,0,18,0\r");
    uint8_t bam[256];
    read_buffer_channel(testname, dr, 2, bam, sizeof(bam));
    printf("%s: the channel read %02X %02X %02X from 18/0\n", testname, bam[0], bam[1], bam[2]);
    REQUIRE((bam[0] == 18) && (bam[1] == 1) && (bam[2] == 'A'));
    expect_command_ok(testname, dr, "U1:2,40,18,0\r");
    read_buffer_channel(testname, dr, 2, bam, sizeof(bam));
    REQUIRE((bam[0] == 18) && (bam[1] == 1) && (bam[2] == 'A'));
    close_file(dr, 2);
}

// SI-094: B-R takes the number of bytes from the first byte of the block and starts at
// the second; B-W stores the buffer pointer minus one there. U1 and U2 do neither.
static void s11_si094_block_length(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI094-BlockLength";
    s11_block_partition(fm, dr, testname, 51, "/Fat/s11_si094.d64", 2);
    uint8_t block[256];
    memset(block, 0, sizeof(block));
    block[0] = 5;
    memcpy(block + 1, "HELLO", 5);
    expect_command_ok(testname, dr, "B-P 2 0\r");
    send_channel_data(dr, 2, block, sizeof(block));
    expect_command_ok(testname, dr, "U2:2,0,1,1\r");
    expect_command_ok(testname, dr, "B-R:2,0,1,1\r");
    uint8_t got[256];
    int n = read_file(dr, 2, got, sizeof(got));
    printf("%s: B-R made %d bytes available\n", testname, n);
    REQUIRE((n == 5) && (memcmp(got, "HELLO", 5) == 0));

    expect_command_ok(testname, dr, "B-P 2 1\r");
    send_channel_data(dr, 2, (const uint8_t *)"ABC", 3);
    expect_command_ok(testname, dr, "B-W:2,0,1,2\r");
    expect_command_ok(testname, dr, "U1:2,0,1,2\r");
    read_buffer_channel(testname, dr, 2, got, 256);
    printf("%s: B-W stored %d in byte 0\n", testname, got[0]);
    REQUIRE((got[0] == 3) && (memcmp(got + 1, "ABC", 3) == 0));
    close_file(dr, 2);
}

extern int iec_interface_configure_calls; // counted by the interface stub

// SI-103: UJ closes the data channels, keeping the partition and its directory, and
// U+shifted J also returns every partition to its root and selects partition 1. Both
// answer 73. Neither may reconfigure the IEC interface, which on the device holds the
// IEC processor in reset, so the command channel has to go on answering.
static void s11_si103_resets(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI103-Resets";
    s11_partition(fm, dr, "si103");
    expect_command_ok(testname, dr, "MD:SUB\r");
    expect_command_ok(testname, dr, "CD:SUB\r");
    open_file(dr, 1, "KEPT");
    get_status(dr);
    expect_status_ok(testname, "KEPT");
    send_channel_data(dr, 1, (const uint8_t *)"abc", 3);

    int configured = iec_interface_configure_calls;
    expect_command_status_prefix(testname, dr, "UJ\r", "73,");
    printf("%s: interface configured %d times by UJ\n", testname, iec_interface_configure_calls - configured);
    REQUIRE(iec_interface_configure_calls == configured);
    expect_command_response(testname, dr, "XPWD\r", "40:/SUB/");
    expect_iec_file(testname, dr, 0, "KEPT", "abc");

    expect_command_status_prefix(testname, dr, "U\xCA\r", "73,");
    REQUIRE(iec_interface_configure_calls == configured);
    expect_command_response(testname, dr, "XPWD\r", "1:/");
    expect_command_response(testname, dr, "CP40\r", "02,PARTITION SELECTED,40,00\r");
    expect_command_response(testname, dr, "XPWD\r", "40:/");
}

// A host file of `len` bytes: an x00 header naming `cbm_name` with `record_length` at
// offset 25 when `cbm_name` is given, then `data`.
static void s11_host_file(FileManager *fm, const char *dir, const char *host, const char *cbm_name,
                          uint8_t record_length, const uint8_t *data, int len)
{
    const char *testname = "Suite11";
    uint8_t content[600];
    int n = 0;
    if (cbm_name) {
        memset(content, 0, 26);
        memcpy(content, "C64File", 7);
        memcpy(content + 8, cbm_name, strlen(cbm_name));
        content[25] = record_length;
        n = 26;
    }
    memcpy(content + n, data, len);
    uint32_t tr;
    REQUIRE(fm->save_file(true, dir, host, content, n + len, &tr) == FR_OK);
}

// The whole of a host file, or -1 when it does not exist.
static int s11_read_host_file(FileManager *fm, const char *dir, const char *host, uint8_t *out, int size)
{
    uint32_t tr = 0;
    if (fm->load_file(dir, host, out, size, &tr) != FR_OK) {
        return -1;
    }
    return (int)tr;
}

// SI-144a: the header reader the C64 loader shares with the drive. A file the loader
// opens has to be moved past its header before its first two bytes are read as a load
// address, which is what x00_skip_header() does for both of them.
static void s11_si144_shared_header(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI144-SharedHeader";
    const char *path = s11_partition(fm, dr, "si144h");
    static const uint8_t payload[] = { 0x01, 0x08, 0x0B, 0x08, 0xAA, 0x00 };
    s11_host_file(fm, path, "GAME.P00", "GAME", 0, payload, sizeof(payload));
    s11_host_file(fm, path, "PLAIN.PRG", NULL, 0, payload, sizeof(payload));
    s11_host_file(fm, path, "FAKE.P00", NULL, 0, payload, sizeof(payload));

    static const struct { const char *host; uint32_t header; } cases[] = {
        { "GAME.P00", X00_HEADER_SIZE }, { "PLAIN.PRG", 0 }, { "FAKE.P00", 0 },
    };
    for (int i = 0; i < 3; i++) {
        File *f = NULL;
        REQUIRE(fm->fopen(path, cases[i].host, FA_READ, &f) == FR_OK);
        uint32_t skipped = x00_skip_header(f, cases[i].host, NULL);
        uint8_t head[2] = { 0, 0 };
        uint32_t got = 0;
        f->read(head, 2, &got);
        fm->fclose(f);
        printf("%s: %s skipped %u bytes, first two are %02X %02X\n",
               testname, cases[i].host, skipped, head[0], head[1]);
        REQUIRE(skipped == cases[i].header);
        REQUIRE((got == 2) && (head[0] == 0x01) && (head[1] == 0x08));
    }
}

// SI-132: an entry in a CBM image whose closed bit is clear lists with a splat in front
// of its type, as every Commodore drive shows a file a write never finished.
static void s11_si132_splat(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI132-Splat";
    s11_block_partition(fm, dr, testname, 45, "/Fat/s11_si132.d64", 2);
    expect_iec_write_ok(testname, dr, 1, "45:CLOSED", "a file that was closed");

    char type[8];
    bool present;
    s11_listing_type(dr, testname, "$45", "CLOSED", type, &present);
    REQUIRE(present && !strcmp(type, "PRG "));


    // Clear the closed bit of the first directory entry, through the block commands, so
    // that the image carries the entry a Commodore leaves behind after a failed write.
    uint8_t sector[256];
    expect_command_ok(testname, dr, "U1:2,0,18,1\r");
    read_buffer_channel(testname, dr, 2, sector, sizeof(sector));
    printf("%s: the first directory entry's type byte is %02X\n", testname, sector[2]);
    REQUIRE(sector[2] == 0x82);
    sector[2] &= 0x7F;
    expect_command_ok(testname, dr, "B-P:2,0\r");
    send_channel_data(dr, 2, sector, sizeof(sector));
    expect_command_ok(testname, dr, "U2:2,0,18,1\r");
    close_file(dr, 2);

    // The splat is the column in front of the type, so the whole field is read here.
    uint8_t listing[4096];
    int got = read_directory_stream(testname, dr, "$45", listing, sizeof(listing));
    const uint8_t *line = s11_listing_line(listing, got, "CLOSED");
    REQUIRE(line != NULL);
    char field[6];
    memcpy(field, line + 26 - 1, 5);
    field[5] = 0;
    printf("%s: an entry whose closed bit is clear lists its type as '%s'\n", testname, field);
    REQUIRE(strcmp(field, "*PRG ") == 0);
    dr->get_file_system()->RemovePartition(45);
}

// SI-137 and SI-145: the two things the drive deliberately does not do on this side.
// Opening `$` on a data channel gives the BASIC listing and not the raw directory
// sectors, and a file the drive creates is written plain and not in an x00 wrapper.
static void s11_deliberate_exclusions(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-DeliberateExclusions";
    const char *path = s11_partition(fm, dr, "excl");
    uint8_t listing[4096];

    // SI-137: the same bytes on channel 0, on channel 2 and on channel 14.
    int n0 = read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    REQUIRE((n0 > 4) && (listing[0] == 1) && (listing[1] == 4));
    for (uint8_t chan = 2; chan <= 14; chan += 12) {
        uint8_t other[4096];
        open_file(dr, chan, "$");
        get_status(dr);
        expect_current_status(testname, "$ on a data channel", "00, OK,00,00\r");
        int n = read_file(dr, chan, other, sizeof(other));
        close_file(dr, chan);
        printf("%s: $ on channel %u is %d bytes, on channel 0 it is %d\n",
               testname, chan, n, n0);
        REQUIRE((n == n0) && (memcmp(other, listing, n0) == 0));
    }

    // SI-145: a new file of every type is written under its host name, with no header.
    static const struct { const char *name; const char *host; } written[] = {
        { "PLAINSEQ,S,W", "plainseq.seq" },
        { "PLAINUSR,U,W", "plainusr.usr" },
        { "PLAINPRG,P,W", "plainprg.prg" },
    };
    for (int i = 0; i < 3; i++) {
        expect_iec_write_ok(testname, dr, 2, written[i].name, "payload");
        uint8_t raw[64];
        int got = s11_read_host_file(fm, path, written[i].host, raw, sizeof(raw));
        printf("%s: %s is %d bytes on the host\n", testname, written[i].host, got);
        REQUIRE((got == 7) && (memcmp(raw, "payload", 7) == 0));
    }
}

// SI-144: a P00, S00, U00 or R00 file that starts with "C64File" lists under the CBM name
// in its header, with the type of its extension and the size of what follows the 26 byte
// header, and opens by that name with the header skipped. A file with such an extension
// and no signature is an ordinary file. Scratch and rename work on the CBM name, and a
// rename rewrites the name in the header.
static void s11_si144_read_x00(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI144-ReadX00";
    const char *path = s11_partition(fm, dr, "si144");
    s11_host_file(fm, path, "GAME.P00", "MY GAME", 0, (const uint8_t *)"PAYLOAD", 7);
    s11_host_file(fm, path, "TEXT.S00", "NOTES", 0, (const uint8_t *)"some text", 9);
    s11_host_file(fm, path, "X.P00", NULL, 0, (const uint8_t *)"not wrapped", 11);

    char type[8];
    bool present;
    s11_listing_type(dr, testname, "$", "MY GAME", type, &present);
    printf("%s: MY GAME listed %d as '%s'\n", testname, present, type);
    REQUIRE(present && !strcmp(type, "PRG "));
    s11_listing_type(dr, testname, "$", "NOTES", type, &present);
    REQUIRE(present && !strcmp(type, "SEQ "));
    s11_listing_type(dr, testname, "$", "X.P00", type, &present);
    printf("%s: X.P00 listed %d as '%s'\n", testname, present, type);
    REQUIRE(present);
    uint8_t listing[4096];
    int got = read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    const uint8_t *line = s11_listing_line(listing, got, "MY GAME");
    REQUIRE(line && (line[0] == (7 % 254) + 2));

    expect_iec_file(testname, dr, 0, "MY GAME", "PAYLOAD");
    expect_iec_file(testname, dr, 2, "NOTES,S", "some text");
    expect_iec_file(testname, dr, 2, "X.P00,S", "not wrapped");

    // An append lands after the data and P counts from the start of the data.
    expect_iec_write_ok(testname, dr, 2, "NOTES,S,A", "!");
    expect_iec_file(testname, dr, 2, "NOTES,S", "some text!");
    open_file(dr, 3, "MY GAME,P");
    get_status(dr);
    expect_status_ok(testname, "MY GAME,P");
    expect_rel_position_status(testname, dr, 3, 3, 0, "00, OK,00,00\r");
    uint8_t rest[16];
    int count = read_file(dr, 3, rest, sizeof(rest));
    rest[(count > 0) && (count < 16) ? count : 0] = 0;
    printf("%s: after P 3 the file reads '%s'\n", testname, (char *)rest);
    REQUIRE((count == 4) && (memcmp(rest, "LOAD", 4) == 0));
    close_file(dr, 3);

    expect_command_ok(testname, dr, "R:TUNES=NOTES\r");
    uint8_t host[64];
    got = s11_read_host_file(fm, path, "TEXT.S00", host, sizeof(host));
    printf("%s: after the rename TEXT.S00 is %d bytes, name '%s'\n", testname, got, (char *)host + 8);
    REQUIRE((got == 36) && (memcmp(host + 8, "TUNES\0", 6) == 0));
    expect_iec_file(testname, dr, 2, "TUNES,S", "some text!");
    expect_command_response(testname, dr, "S:TUNES\r", "01, FILES SCRATCHED,01,00\r");
    REQUIRE(s11_read_host_file(fm, path, "TEXT.S00", host, sizeof(host)) < 0);
    expect_command_response(testname, dr, "S:MY*\r", "01, FILES SCRATCHED,01,00\r");
    REQUIRE(s11_read_host_file(fm, path, "GAME.P00", host, sizeof(host)) < 0);
}

// SI-084 and SI-146: a relative file is read in this firmware's two byte layout, sd2iec's
// one byte layout, or inside an R00 wrapper, which an open with a record length finds by
// its CBM name rather than creating the file again beside it.
static void s11_si084_rel_layouts(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI084-RelLayouts";
    const char *path = s11_partition(fm, dr, "si084");
    const int lengths[] = { 2, 3, 127, 254 };
    for (int i = 0; i < 4; i++) {
        int r = lengths[i];
        for (int layout = 1; layout <= 2; layout++) {
            // A zero second byte first: that is the file the old reader misread silently.
            for (int zero_first = 1; zero_first >= 0; zero_first--) {
                uint8_t data[600];
                memset(data, 0, sizeof(data));
                data[0] = (uint8_t)r;
                int n = layout;
                for (int rec = 0; rec < 2; rec++) {
                    for (int b = 0; b < r; b++) {
                        data[n + b] = (uint8_t)('A' + rec);
                    }
                    if (zero_first && (rec == 0)) {
                        data[n] = 0; // byte 1 of a one byte layout file is then zero too
                    }
                    n += r;
                }
                char host[24], name[16];
                snprintf(name, sizeof(name), "R%dL%dZ%d", r, layout, zero_first);
                snprintf(host, sizeof(host), "%s.rel", name);
                uint32_t tr;
                REQUIRE(fm->save_file(true, path, host, data, n, &tr) == FR_OK);
                expect_rel_open(testname, dr, 2, name, 0);
                expect_rel_position_status(testname, dr, 2, 2, 1, "00, OK,00,00\r");
                uint8_t expected[256];
                memset(expected, 'B', r);
                uint8_t got[256];
                memset(got, 0, sizeof(got));
                int count = read_file(dr, 2, got, sizeof(got));
                printf("%s: %s record 2 read %d bytes starting %02X\n", testname, name, count, got[0]);
                REQUIRE((count == r) && (memcmp(got, expected, r) == 0));
                close_file(dr, 2);
            }
        }
    }

    // A record length of 1 cannot be told apart by size; it is taken as the two byte layout.
    uint32_t tr;
    REQUIRE(fm->save_file(true, path, "ONEBYTE.rel", (const uint8_t *)"\x01\x00" "AB", 4, &tr) == FR_OK);
    expect_rel_open(testname, dr, 2, "ONEBYTE", 0);
    expect_rel_position_status(testname, dr, 2, 2, 1, "00, OK,00,00\r");
    uint8_t one[4];
    REQUIRE((read_file(dr, 2, one, sizeof(one)) == 1) && (one[0] == 'B'));
    close_file(dr, 2);

    // R00: the record length is at offset 25 and the records follow the header.
    s11_host_file(fm, path, "WRAP.R00", "WRAPREL", 3, (const uint8_t *)"aaabbb", 6);
    expect_rel_open(testname, dr, 2, "WRAPREL", 0);
    expect_rel_position_status(testname, dr, 2, 2, 1, "00, OK,00,00\r");
    uint8_t rec[8];
    int count = read_file(dr, 2, rec, sizeof(rec));
    rec[(count > 0) && (count < 8) ? count : 0] = 0;
    printf("%s: WRAPREL record 2 read %d bytes '%s'\n", testname, count, (char *)rec);
    REQUIRE((count == 3) && (memcmp(rec, "bbb", 3) == 0));
    close_file(dr, 2);

    expect_rel_open(testname, dr, 2, "WRAPREL", 3);
    expect_rel_position_status(testname, dr, 2, 1, 1, "00, OK,00,00\r");
    count = read_file(dr, 2, rec, sizeof(rec));
    REQUIRE((count == 3) && (memcmp(rec, "aaa", 3) == 0));
    close_file(dr, 2);
    uint8_t host[64];
    REQUIRE(s11_read_host_file(fm, path, "WRAPREL.rel", host, sizeof(host)) < 0);
}

// SI-144 on the paths that do not find a file through its CBM name: an open without a
// type, for which ConstructPath() makes a pattern; the creation of a name an x00 file
// carries; a copy; a rename into another directory; and a host name longer than a
// listing entry used to hold.
static void s11_x00_paths(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI144-X00Paths";
    const char *path = s11_partition(fm, dr, "x00paths");
    uint8_t host[128];
    s11_host_file(fm, path, "DATA.S00", "DATA", 0, (const uint8_t *)"payload", 7);
    expect_iec_file(testname, dr, 2, "DATA", "payload");
    // A copy takes the data and the type, not the header.
    expect_command_ok(testname, dr, "C:COPY=DATA\r");
    int got = s11_read_host_file(fm, path, "COPY.seq", host, sizeof(host));
    printf("%s: COPY.seq is %d bytes\n", testname, got);
    REQUIRE((got == 7) && (memcmp(host, "payload", 7) == 0));
    // The name is taken: 63 without @, and with @ the new file takes the x00 file's place.
    open_file(dr, 2, "DATA,S,W");
    get_status(dr);
    expect_current_status(testname, "DATA,S,W", "63,FILE EXISTS,00,00\r");
    close_file(dr, 2);
    expect_iec_write_ok(testname, dr, 2, "@:DATA,S,W", "new");
    REQUIRE(s11_read_host_file(fm, path, "DATA.S00", host, sizeof(host)) < 0);
    expect_iec_file(testname, dr, 2, "DATA,S", "new");
    // A rename into another directory moves an x00 file, under its host name.
    s11_host_file(fm, path, "MOVE.P00", "MOVER", 0, (const uint8_t *)"m", 1);
    expect_command_ok(testname, dr, "MD:SUB\r");
    expect_command_ok(testname, dr, "R/SUB/:MOVED=MOVER\r");
    expect_iec_file(testname, dr, 0, "/SUB/:MOVED", "m");
    // A host name longer than forty characters still lists under the CBM name.
    s11_host_file(fm, path, "A HOST NAME THAT IS LONGER THAN FORTY CHARACTERS.P00", "LONGWRAP", 0,
                  (const uint8_t *)"l", 1);
    expect_directory_contains(testname, dr, "$", "\"LONGWRAP\"");
}

// SI-084 inside a disk image: the image presents the two byte layout, so a size that is
// not a whole number of records does not select the one byte layout.
static void s11_rel_in_image(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-SI084-RelInImage";
    create_formatted_image(fm, "/Fat/s11_relimg.d64", "RELIMG", 683, e_image_d64);
    dr->add_partition(56, "/Fat/s11_relimg.d64", "RELIMG");
    expect_command_status_prefix(testname, dr, "CP56\r", "02,PARTITION SELECTED");
    expect_rel_open(testname, dr, 2, "RECS", 4);
    expect_rel_position_status(testname, dr, 2, 2, 1, "50,RECORD NOT PRESENT,00,00\r");
    expect_rel_position_status(testname, dr, 2, 1, 1, "00, OK,00,00\r");
    expect_rel_write(testname, dr, 2, (const uint8_t *)"AAAA", 4);
    expect_rel_write(testname, dr, 2, (const uint8_t *)"BBBB", 4);
    close_file(dr, 2);

    // Shorten the last data sector of the file by one byte.
    open_buffer_channel(testname, dr, 3);
    expect_command_ok(testname, dr, "U1:3,0,18,1\r");
    uint8_t dir[256];
    read_buffer_channel(testname, dr, 3, dir, sizeof(dir));
    int entry = -1;
    for (int e = 0; e < 8; e++) {
        if (((dir[2 + 32 * e] & 7) == 4) && !memcmp(dir + 5 + 32 * e, "RECS\xA0", 5)) {
            entry = e;
        }
    }
    REQUIRE(entry >= 0);
    int track = dir[3 + 32 * entry], sector = dir[4 + 32 * entry];
    uint8_t block[256];
    char cmd[32];
    for (int hops = 0; hops < 100; hops++) {
        snprintf(cmd, sizeof(cmd), "U1:3,0,%d,%d\r", track, sector);
        expect_command_ok(testname, dr, cmd);
        read_buffer_channel(testname, dr, 3, block, sizeof(block));
        if (!block[0]) {
            break;
        }
        track = block[0];
        sector = block[1];
    }
    printf("%s: the last data sector %d/%d ends at byte %d\n", testname, track, sector, block[1]);
    REQUIRE(block[0] == 0);
    block[1]--;
    expect_command_ok(testname, dr, "B-P 3 0\r");
    send_channel_data(dr, 3, block, sizeof(block));
    snprintf(cmd, sizeof(cmd), "U2:3,0,%d,%d\r", track, sector);
    expect_command_ok(testname, dr, cmd);
    close_file(dr, 3);

    expect_rel_open(testname, dr, 2, "RECS", 0);
    expect_rel_position_status(testname, dr, 2, 1, 1, "00, OK,00,00\r");
    expect_rel_read(testname, dr, 2, (const uint8_t *)"AAAA", 4);
    close_file(dr, 2);
}

// CR-4: a listing that is abandoned releases its directory when the channel closes, so
// the heap is back where it was once the channel is closed. A full listing first puts
// everything the file manager caches for the directory in place.
static size_t s11_heap_in_use(void)
{
    struct mallinfo2 m = mallinfo2();
    return m.uordblks;
}

static void s11_cr4_abandoned_listing(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-CR4-AbandonedListing";
    s11_partition(fm, dr, "cr4");
    expect_iec_write_ok(testname, dr, 1, "ONE", "1");
    uint8_t listing[4096];
    read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    size_t before = s11_heap_in_use();
    for (int i = 0; i < 2; i++) {
        uint8_t part[8];
        open_file(dr, 0, "$");
        get_status(dr);
        expect_status_ok(testname, "$");
        REQUIRE(read_file_limited(dr, 0, part, sizeof(part)) == sizeof(part));
        close_file(dr, 0);
    }
    size_t after = s11_heap_in_use();
    printf("%s: %d bytes in use before, %d after the channel closed\n", testname, (int)before, (int)after);
    REQUIRE(after <= before);
}

// CR-5: a directory open that fails after an abandoned listing must not leave the channel
// pointing at the directory it freed, which the next open would free again.
static void s11_cr5_failed_directory_open(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-CR5-FailedDirectoryOpen";
    s11_partition(fm, dr, "cr5");
    expect_iec_write_ok(testname, dr, 1, "ONE", "1");
    for (int i = 0; i < 3; i++) {
        uint8_t part[8];
        open_file(dr, 0, "$");
        get_status(dr);
        expect_status_ok(testname, "$");
        REQUIRE(read_file_limited(dr, 0, part, sizeof(part)) == sizeof(part));
        open_file(dr, 0, "$/NOSUCHDIR/");
        get_status(dr);
        printf("%s: $/NOSUCHDIR/ answered %s", testname, last_status);
        REQUIRE(strncmp(last_status, "00,", 3) != 0);
        close_file(dr, 0);
    }
    // The UCI target opens without closing first, so there the directory open itself must
    // leave nothing dangling.
    IecChannel *channel = dr->get_data_channel(0);
    for (int i = 0; i < 3; i++) {
        REQUIRE(channel->ext_open_file("$") == 1);
        REQUIRE(channel->ext_open_file("$/NOSUCHDIR/") == 0);
    }
    REQUIRE(channel->ext_open_file("$") == 1);
    channel->ext_close_file();
    expect_directory_contains(testname, dr, "$", "\"ONE\"");
    expect_command_response(testname, dr, "XPWD\r", "40:/");
}

// CR-6: the IEC task and the GUI task reach the same channels and partitions, so the
// drive holds its lock whenever one of its entry points reaches storage. The FAT file
// counts the block accesses made while a watched drive is inside an entry point without
// its lock.
int s11_lock_watch_unlocked = 0;
int s11_lock_watch_locked = 0;
IecDrive *s11_lock_watch = NULL;
extern int iec_drive_lock_depth(IecDrive *drive) __attribute__((weak));

static void s11_cr6_lock(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-CR6-Lock";
    s11_partition(fm, dr, "cr6");
    if (!iec_drive_lock_depth) {
        printf("%s: the drive has no lock\n", testname);
    }
    REQUIRE(iec_drive_lock_depth);
    s11_lock_watch_unlocked = 0;
    s11_lock_watch_locked = 0;
    s11_lock_watch = dr;
    expect_iec_write_ok(testname, dr, 1, "LOCKED", "some data");
    expect_iec_file(testname, dr, 0, "LOCKED", "some data");
    expect_directory_contains(testname, dr, "$", "\"LOCKED\"");
    expect_command_ok(testname, dr, "MD:SUB\r");
    open_file(dr, 1, "PENDING");
    get_status(dr);
    uint8_t block[600];
    memset(block, 'p', sizeof(block));
    send_channel_data(dr, 1, block, sizeof(block));
    dr->reset(); // the GUI's Reset closes the file
    s11_lock_watch = NULL;
    printf("%s: %d block accesses inside the lock, %d outside it\n", testname,
           s11_lock_watch_locked, s11_lock_watch_unlocked);
    REQUIRE((s11_lock_watch_unlocked == 0) && (s11_lock_watch_locked > 0));
    REQUIRE(iec_drive_lock_depth(dr) == 0);
    expect_command_response(testname, dr, "CP40\r", "02,PARTITION SELECTED,40,00\r");
    expect_directory_contains(testname, dr, "$", "\"PENDING\"");
}

// CR-8: a scratch by name is driven by the directory, so it removes every unlocked entry of
// that name and stops, and never deletes a locked one.
static void s11_cr8_scratch_scan(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-CR8-ScratchScan";
    const char *image = "/Fat/s11_cr8.d64";
    create_formatted_image(fm, image, "TWINS", 683, e_image_d64);
    dr->add_partition(54, image, "TWINS");
    expect_command_status_prefix(testname, dr, "CP54\r", "02,PARTITION SELECTED");
    expect_iec_write_ok(testname, dr, 1, "TWIN", "prg");
    expect_iec_write_ok(testname, dr, 2, "TWIN,S,W", "seq");
    expect_iec_write_ok(testname, dr, 2, "OTHER,S,W", "other");
    expect_command_ok(testname, dr, "L:TWIN\r"); // the first entry, the PRG
    char type[8];
    bool present;
    s11_listing_type(dr, testname, "$", "TWIN", type, &present);
    REQUIRE(present && !strcmp(type, "PRG<"));
    const char *response = send_command(dr, "S:TWIN\r");
    printf("%s: S:TWIN answered %s", testname, response);
    REQUIRE(strcmp(response, "01, FILES SCRATCHED,01,00\r") == 0);
    uint8_t listing[4096];
    int got = read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    int twins = 0;
    for (int offset = 32; offset + 32 <= got; offset += 32) {
        twins += (memmem(listing + offset, 32, "\"TWIN\"", 6) != NULL);
    }
    s11_listing_type(dr, testname, "$", "TWIN", type, &present);
    printf("%s: %d TWIN entries left, the first typed '%s'\n", testname, twins, type);
    REQUIRE((twins == 1) && present && !strcmp(type, "PRG<"));
    s11_listing_type(dr, testname, "$", "OTHER", type, &present);
    REQUIRE(present);
    expect_command_ok(testname, dr, "L:TWIN\r");
    expect_command_response(testname, dr, "S:TWIN\r", "01, FILES SCRATCHED,01,00\r");

    // Two entries of one name and type, the first locked, as a damaged image can have
    // them. A delete by name removes the first, so the unlocked second is not scratched
    // either, and the locked one stays.
    expect_iec_write_ok(testname, dr, 1, "PAIR", "first");
    expect_iec_write_ok(testname, dr, 2, "PAIR,S,W", "second");
    expect_command_ok(testname, dr, "L:PAIR\r");
    open_buffer_channel(testname, dr, 3);
    expect_command_ok(testname, dr, "U1:3,0,18,1\r");
    uint8_t dir[256];
    read_buffer_channel(testname, dr, 3, dir, sizeof(dir));
    int seq = -1;
    for (int e = 0; e < 8; e++) {
        if (((dir[2 + 32 * e] & 7) == 1) && !memcmp(dir + 5 + 32 * e, "PAIR\xA0", 5)) {
            seq = e;
        }
    }
    REQUIRE(seq >= 0);
    dir[2 + 32 * seq] = (dir[2 + 32 * seq] & ~7) | 2; // the SEQ entry becomes a second PRG
    expect_command_ok(testname, dr, "B-P 3 0\r");
    send_channel_data(dr, 3, dir, sizeof(dir));
    expect_command_ok(testname, dr, "U2:3,0,18,1\r");
    close_file(dr, 3);
    response = send_command(dr, "S:PAIR\r");
    printf("%s: with a locked PAIR first, S:PAIR answered %s", testname, response);
    s11_listing_type(dr, testname, "$", "PAIR", type, &present);
    printf("%s: the first PAIR is now typed '%s'\n", testname, type);
    REQUIRE(present && !strcmp(type, "PRG<"));
}

/* =============================================================================
 * Software IEC failure log.
 *
 * The drive writes one line, starting with "SoftIEC: ", for a command that leaves an
 * error, an open that fails and the first failure of a channel, and nothing for an
 * operation that succeeds. This suite captures what the drive writes while it runs
 * operations of both kinds.
 * ============================================================================= */

static char log_capture[256 * 1024];

static void log_capture_begin(int &saved_fd, FILE *&sink)
{
    const char *testname = "Suite11-FailureLog";
    fflush(stdout);
    saved_fd = dup(fileno(stdout));
    sink = fopen("log_capture.txt", "w+");
    REQUIRE(saved_fd >= 0);
    REQUIRE(sink != NULL);
    dup2(fileno(sink), fileno(stdout));
}

static void log_capture_end(int saved_fd, FILE *sink)
{
    const char *testname = "Suite11-FailureLog";
    fflush(stdout);
    dup2(saved_fd, fileno(stdout));
    close(saved_fd);
    fseek(sink, 0, SEEK_SET);
    size_t got = fread(log_capture, 1, sizeof(log_capture) - 1, sink);
    log_capture[got] = 0;
    fclose(sink);
    remove("log_capture.txt");
}

static void expect_log_line(const char *testname, const char *what, const char *needle)
{
    if (strstr(log_capture, needle) == NULL) {
        printf("%s: %s: no log line containing '%s' in:\n%s\n", testname, what, needle, log_capture);
    }
    REQUIRE(strstr(log_capture, needle) != NULL);
}

// Lines of the failure log, and of the diagnostics that came before it.
static int count_log_lines(void)
{
    int n = 0;
    for (const char *p = log_capture; (p = strstr(p, "SoftIEC: ")) != NULL; p++) {
        n++;
    }
    for (const char *p = log_capture; (p = strstr(p, "SOFTIEC-TRACE")) != NULL; p++) {
        n++;
    }
    return n;
}

static void s11_failure_log(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-FailureLog";

    // A partition of its own, so the state the earlier suites left behind cannot
    // change what this one sees.
    save_fixture_file(fm, "/Temp", "LOGGED.seq", "LOG PAYLOAD");
    dr->add_partition(9, "/Temp", "LOGPART");
    expect_command_response(testname, dr, "CP9\r", "02,PARTITION SELECTED,09,00\r");

    int saved_fd = -1;
    FILE *sink = NULL;

    // Operations that succeed write nothing: a command with a reply, the binary Change
    // Partition, UI, which answers 73, and an open, a read and a close.
    log_capture_begin(saved_fd, sink);
    send_command(dr, "G-P\r");
    static const uint8_t change_partition[] = { 'C', 0xD0, 0x09 };
    send_command_data(dr, change_partition, 3);
    get_status(dr);
    send_command(dr, "UI\r");
    uint8_t body[64];
    open_file(dr, 2, "9:LOGGED,S,R");
    get_status(dr);
    int got = read_file(dr, 2, body, sizeof(body));
    close_file(dr, 2);
    log_capture_end(saved_fd, sink);
    REQUIRE(got == 11);
    printf("%s: %d log lines for operations that succeeded\n", testname, count_log_lines());
    REQUIRE(count_log_lines() == 0);

    // A command that fails is logged with its bytes, the carriage return still told apart
    // from a printable byte, and the answer.
    log_capture_begin(saved_fd, sink);
    send_command(dr, "ZZ\r");
    log_capture_end(saved_fd, sink);
    expect_log_line(testname, "failed command",
                    "SoftIEC: command failed dev=11 chan=15 part=9 dir=\"/\" len=3 txt=\"ZZ\\r\" -> 31,SYNTAX ERROR,00,00");
    REQUIRE(count_log_lines() == 1);

    // An open that fails is logged with the name as the bus delivered it.
    log_capture_begin(saved_fd, sink);
    open_file(dr, 3, "9:NOSUCH,S,R");
    get_status(dr);
    close_file(dr, 3);
    log_capture_end(saved_fd, sink);
    expect_log_line(testname, "failed open",
                    "SoftIEC: open failed dev=11 chan=3 part=9 dir=\"/\" len=12 txt=\"9:NOSUCH,S,R\" -> 62,FILE NOT FOUND,00,00");
    REQUIRE(count_log_lines() == 1);

    // SI-152: a command longer than the rendering reports its real length, and the
    // rendering says where it was cut.
    char long_cmd[201];
    memset(long_cmd, 0x01, 200); // four characters each in the rendering
    memcpy(long_cmd, "ZZ", 2);
    long_cmd[200] = 0;
    log_capture_begin(saved_fd, sink);
    send_command(dr, long_cmd);
    log_capture_end(saved_fd, sink);
    expect_log_line(testname, "long command length", "SoftIEC: command failed dev=11 chan=15 part=9 dir=\"/\" len=200 ");
    expect_log_line(testname, "long command cut", "\\x01..\" -> 31");

}

static ConfigStore *s11_softiec_settings(void)
{
    const char *testname = "Suite11";
    ConfigStore *cfg = ConfigManager::getConfigManager()->find_store((uint32_t)0x49454300);
    REQUIRE(cfg);
    return cfg;
}

// With "Log Every Operation" on, every command, open and close writes a line with the
// current partition and directory, a command that answers with data adds its reply, an open
// adds the host file or the first bytes of a listing, and a listing logs its last line.
// With the setting off again, operations that succeed write nothing.
static void s11_operation_log(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-OperationLog";
    save_fixture_file(fm, "/Temp", "OPLOG.seq", "LOG PAYLOAD");
    dr->add_partition(9, "/Temp", "LOGPART");
    expect_command_response(testname, dr, "CP9\r", "02,PARTITION SELECTED,09,00\r");
    ConfigStore *cfg = s11_softiec_settings();
    cfg->set_value(0x55, 1);
    REQUIRE(dr->log_every_operation());

    int saved_fd = -1;
    FILE *sink = NULL;
    log_capture_begin(saved_fd, sink);
    send_command(dr, "CD//\r");
    const uint8_t mr[6] = { 'M', '-', 'R', 0x00, 0x05, 0x02 };
    send_command_data(dr, mr, sizeof(mr));
    uint8_t reply[8];
    read_command_channel(dr, reply, sizeof(reply));
    uint8_t body[64];
    open_file(dr, 2, "9:OPLOG,S,R");
    get_status(dr);
    read_file(dr, 2, body, sizeof(body));
    close_file(dr, 2);
    uint8_t listing[4096];
    read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    log_capture_end(saved_fd, sink);
    printf("%s: %d lines\n", testname, count_log_lines());
    expect_log_line(testname, "command", "SoftIEC: command dev=11 chan=15 part=9 dir=\"/\" len=5 txt=\"CD//\\r\" -> 00, OK,00,00");
    expect_log_line(testname, "reply", "txt=\"M-R\\0\\x05\\x02\" reply=\"\\0\\0\" -> 00, OK,00,00");
    expect_log_line(testname, "open", "SoftIEC: open dev=11 chan=2 part=9 dir=\"/\" len=11 txt=\"9:OPLOG,S,R\" host=\"/Temp/OPLOG.seq\"");
    expect_log_line(testname, "close", "SoftIEC: close dev=11 chan=2 ");
    expect_log_line(testname, "listing", "txt=\"$\" data=\"\\x01\\x04\\x01\\x01");
    expect_log_line(testname, "listing end", "SoftIEC: listing end dev=11 chan=0 part=9 dir=\"/\" len=32 txt=\"");
    REQUIRE(count_log_lines() >= 7);

    // Every line ends in " #" and a sequence number one higher than the line before it, so
    // a reader of the device log can tell a line lost or delivered twice from one the drive
    // wrote twice.
    int previous = -1;
    int numbered = 0;
    for (const char *p = log_capture; (p = strstr(p, "SoftIEC: ")) != NULL; p++) {
        const char *end = strchr(p, '\n');
        const char *hash = NULL;
        for (const char *q = p; q && *q && (!end || q < end); q++) {
            if (*q == '#') {
                hash = q;
            }
        }
        REQUIRE(hash != NULL);
        int sequence = atoi(hash + 1);
        if (previous >= 0) {
            REQUIRE(sequence == previous + 1);
        }
        previous = sequence;
        numbered++;
    }
    printf("%s: %d lines numbered, the last %d\n", testname, numbered, previous);
    REQUIRE(numbered == count_log_lines());

    cfg->set_value(0x55, 0);
    REQUIRE(!dr->log_every_operation());
    log_capture_begin(saved_fd, sink);
    send_command(dr, "CD//\r");
    read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    log_capture_end(saved_fd, sink);
    REQUIRE(count_log_lines() == 0);
}

// Changing Log Every Operation must not reconfigure the IEC interface, which on the device
// holds the IEC processor in reset and so drops a transfer that is on the bus. A change of
// the device number or of the enable still does.
static void s11_operation_log_no_reconfigure(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-OperationLogNoReconfigure";
    ConfigStore *cfg = s11_softiec_settings();
    int configured = iec_interface_configure_calls;
    cfg->set_value(0x55, 1);
    dr->effectuate_settings();
    cfg->set_value(0x55, 0);
    dr->effectuate_settings();
    printf("%s: interface configured %d times by two changes of the log setting\n", testname,
           iec_interface_configure_calls - configured);
    REQUIRE(iec_interface_configure_calls == configured);

    int bus_id = cfg->get_value(0x52);
    cfg->set_value(0x52, bus_id + 1);
    dr->effectuate_settings();
    REQUIRE(iec_interface_configure_calls == configured + 1);
    REQUIRE(dr->get_address() == bus_id + 1);
    cfg->set_value(0x52, bus_id);
    dr->effectuate_settings();
    REQUIRE(iec_interface_configure_calls == configured + 2);
    REQUIRE(dr->get_address() == bus_id);
    expect_command_status_prefix(testname, dr, "UI\r", "73,");
}

// The operation log with the longest inputs it takes: a working directory near the length
// a command can name, a 253 byte command, names that fill the buffer, and replies of 256
// bytes. Every line must stay within its buffers; the AddressSanitizer build of this suite
// fails on any byte written or read outside them.
static void s11_operation_log_bounds(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-OperationLogBounds";
    s11_partition(fm, dr, "logbounds");
    ConfigStore *cfg = s11_softiec_settings();
    cfg->set_value(0x55, 1);
    // Directories of 40 characters, deeper than one rendering holds.
    char name[48];
    char cmd[300];
    for (int depth = 0; depth < 7; depth++) {
        memset(name, 'D' + depth, 40);
        name[40] = 0;
        snprintf(cmd, sizeof(cmd), "MD:%s\r", name);
        send_command(dr, cmd);
        snprintf(cmd, sizeof(cmd), "CD:%s\r", name);
        send_command(dr, cmd);
    }
    open_file(dr, 2, "LONGFILE,S,W");
    get_status(dr);
    send_channel_data(dr, 2, (const uint8_t *)"DATA", 4);
    close_file(dr, 2);
    int saved_fd = -1;
    FILE *sink = NULL;
    log_capture_begin(saved_fd, sink);
    // The host path of this file is longer than one rendering holds.
    open_file(dr, 2, "LONGFILE,S,R");
    get_status(dr);
    close_file(dr, 2);
    memset(cmd, 0xFF, 253);
    cmd[0] = 'Z';
    cmd[253] = 0;
    send_command(dr, cmd);
    uint8_t mr[6] = { 'M', '-', 'R', 0x00, 0xFE, 0xFF };
    uint8_t reply[300];
    send_command_data(dr, mr, sizeof(mr));
    read_command_channel(dr, reply, sizeof(reply));
    mr[5] = 0x00;
    send_command_data(dr, mr, sizeof(mr));
    read_command_channel(dr, reply, sizeof(reply));
    char open_name[300];
    memset(open_name, 0xA5, 253);
    open_name[253] = 0;
    open_file(dr, 2, open_name);
    get_status(dr);
    close_file(dr, 2);
    open_name[260] = 0;
    memset(open_name, 'N', 260);
    open_file(dr, 3, open_name);
    get_status(dr);
    close_file(dr, 3);
    uint8_t listing[4096];
    read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    log_capture_end(saved_fd, sink);
    cfg->set_value(0x55, 0);
    // No line is longer than the three renderings, the answer and the fixed text allow.
    int longest = 0;
    for (const char *p = log_capture; (p = strstr(p, "SoftIEC: ")) != NULL; p++) {
        const char *end = strchr(p, '\n');
        int n = end ? (int)(end - p) : (int)strlen(p);
        longest = (n > longest) ? n : longest;
    }
    printf("%s: %d lines, the longest %d characters\n", testname, count_log_lines(), longest);
    REQUIRE(count_log_lines() >= 7);
    REQUIRE(longest < 3 * SOFTIEC_LOG_TEXT_SIZE + 200);
    expect_log_line(testname, "long directory cut", "DDDDDDDD/EEEE");
    expect_log_line(testname, "long host path cut", "host=\"/Fat/s11_logbounds/DDDD");
    expect_command_response(testname, dr, "UI\r", "73,U64HD ULTIMATE DOS V2.0,00,00\r");
}

// A file inside a disk image whose chain links to a track the disk does not have. Working
// out its size indexed the loop detection map with -1 (found by Suite11-Soak). The drive
// must answer, and in the AddressSanitizer build nothing may be written outside the map.
static void s11_crash_damaged_chain(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-Crash-DamagedChain";
    create_formatted_image(fm, "/Fat/s11_chain.d64", "CHAIN", 683, e_image_d64);
    dr->add_partition(55, "/Fat/s11_chain.d64", "CHAIN");
    expect_command_status_prefix(testname, dr, "CP55\r", "02,PARTITION SELECTED");
    uint8_t data[600];
    memset(data, 'c', sizeof(data));
    open_file(dr, 2, "LINKED,S,W");
    get_status(dr);
    send_channel_data(dr, 2, data, sizeof(data));
    close_file(dr, 2);
    // The file's first sector is the first entry of the directory sector 18/1.
    open_buffer_channel(testname, dr, 3);
    expect_command_ok(testname, dr, "U1:3,0,18,1\r");
    uint8_t dir[256];
    read_buffer_channel(testname, dr, 3, dir, sizeof(dir));
    int track = dir[3], sector = dir[4];
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "U1:3,0,%d,%d\r", track, sector);
    expect_command_ok(testname, dr, cmd);
    uint8_t block[256];
    read_buffer_channel(testname, dr, 3, block, sizeof(block));
    printf("%s: LINKED starts at %d/%d and links to %d/%d; now to 99/0\n", testname, track, sector, block[0], block[1]);
    block[0] = 99;
    block[1] = 0;
    expect_command_ok(testname, dr, "B-P 3 0\r");
    send_channel_data(dr, 3, block, sizeof(block));
    snprintf(cmd, sizeof(cmd), "U2:3,0,%d,%d\r", track, sector);
    expect_command_ok(testname, dr, cmd);
    close_file(dr, 3);
    // An append works out the size of the file first.
    open_file(dr, 2, "LINKED,S,A");
    get_status(dr);
    printf("%s: the append answered %s", testname, last_status);
    close_file(dr, 2);
    open_file(dr, 2, "LINKED,S,R");
    get_status(dr);
    uint8_t back[1024];
    int got = read_file(dr, 2, back, sizeof(back));
    close_file(dr, 2);
    printf("%s: reading the damaged file gave %d bytes\n", testname, got);
    expect_command_response(testname, dr, "UI\r", "73,U64HD ULTIMATE DOS V2.0,00,00\r");
}

// A host file whose name is longer than the 40 bytes a listing keeps of it. The name was
// copied without a terminator, so every use of it read past the buffer (found by
// Suite11-Soak).
static void s11_crash_long_host_name(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-Crash-LongHostName";
    const char *path = s11_partition(fm, dr, "longname");
    uint32_t tr;
    REQUIRE(fm->save_file(true, path, "A NAME THAT IS MUCH LONGER THAN FORTY BYTES ON THE HOST.prg",
                          (const uint8_t *)"x", 1, &tr) == FR_OK);
    uint8_t listing[4096];
    int got = read_directory_stream(testname, dr, "$", listing, sizeof(listing));
    printf("%s: the listing is %d bytes\n", testname, got);
    REQUIRE(got > 64);
    // The opens and commands that find the file by its CBM name and then work out its
    // type copied the name into 48 bytes without a terminator.
    open_file(dr, 2, "A NAME THAT*");
    get_status(dr);
    printf("%s: a typeless open answered %s", testname, last_status);
    close_file(dr, 2);
    expect_command_ok(testname, dr, "C:COPY=A NAME THAT*\r");
    expect_iec_write_ok(testname, dr, 1, "@:A NAME THAT*", "y");
    expect_command_ok(testname, dr, "R:SHORT=A NAME THAT*\r");
    expect_iec_file(testname, dr, 0, "SHORT", "y");
    expect_command_response(testname, dr, "UI\r", "73,U64HD ULTIMATE DOS V2.0,00,00\r");
}

// P on a file inside a disk image to a position past its end. The last sector's count of
// bytes left went negative and was copied as a length (found by the crash review).
static void s11_crash_seek_past_end_image(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-Crash-SeekPastEndImage";
    create_formatted_image(fm, "/Fat/s11_seek.d64", "SEEK", 683, e_image_d64);
    dr->add_partition(57, "/Fat/s11_seek.d64", "SEEK");
    expect_command_status_prefix(testname, dr, "CP57\r", "02,PARTITION SELECTED");
    char payload[101];
    memset(payload, 'A', 100);
    payload[100] = 0;
    expect_iec_write_ok(testname, dr, 2, "SMALL,S,W", payload);
    open_file(dr, 2, "SMALL,S,R");
    get_status(dr);
    const uint8_t p200[6] = { 'P', 2, 200, 0, 0, 0 };
    send_command_data(dr, p200, sizeof(p200));
    get_status(dr);
    printf("%s: P to byte 200 of a 100 byte file answered %s", testname, last_status);
    uint8_t back[512];
    int got = read_file(dr, 2, back, sizeof(back));
    printf("%s: reading on gave %d bytes\n", testname, got);
    REQUIRE(got <= 1);
    close_file(dr, 2);
    expect_command_response(testname, dr, "UI\r", "73,U64HD ULTIMATE DOS V2.0,00,00\r");
}

// A listing of a disk image partition, part read, while other channels list more images
// than the file manager keeps mounted. The first image was released under its open
// directory (found by the crash review).
static void s11_crash_evict_open_listing(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-Crash-EvictOpenListing";
    char path[64], name[16], cmd[24];
    for (int i = 0; i < 10; i++) {
        snprintf(path, sizeof(path), "/Fat/s11_evict%d.d64", i);
        snprintf(name, sizeof(name), "EV%d", i);
        create_formatted_image(fm, path, name, 683, e_image_d64);
        dr->add_partition(70 + i, path, name);
        snprintf(cmd, sizeof(cmd), "%d:FILE%d,S,W", 70 + i, i);
        expect_iec_write_ok(testname, dr, 2, cmd, "X");
    }
    open_file(dr, 0, "$70");
    get_status(dr);
    uint8_t part[40];
    REQUIRE(read_file_limited(dr, 0, part, sizeof(part)) == sizeof(part));
    uint8_t listing[4096];
    for (int i = 1; i < 10; i++) {
        snprintf(cmd, sizeof(cmd), "$%d", 70 + i);
        open_file(dr, 2, cmd);
        get_status(dr);
        read_file(dr, 2, listing, sizeof(listing));
        close_file(dr, 2);
    }
    memset(listing, 0, sizeof(listing));
    int got = read_file(dr, 0, listing, sizeof(listing));
    printf("%s: the first listing went on for %d more bytes\n", testname, got);
    close_file(dr, 0);
    REQUIRE(memmem(listing, got, "BLOCKS FREE.", 12) != NULL);
}

// A copy that names a source in a partition that does not exist left the target open and
// its 32 KB copy buffer allocated, every time.
static void s11_crash_copy_missing_partition(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-Crash-CopyMissingPartition";
    s11_partition(fm, dr, "copyleak");
    expect_iec_write_ok(testname, dr, 2, "SRC,S,W", "source");
    expect_command_status_prefix(testname, dr, "C:WARM=SRC,200:X\r", "77,");
    size_t before = s11_heap_in_use();
    char cmd[32];
    for (int i = 0; i < 20; i++) {
        snprintf(cmd, sizeof(cmd), "C:N%d=SRC,200:X\r", i);
        expect_command_status_prefix(testname, dr, cmd, "77,");
    }
    size_t after = s11_heap_in_use();
    printf("%s: %d bytes in use before 20 copies, %d after\n", testname, (int)before, (int)after);
    REQUIRE(after < before + 32768);
}

// An open through the UCI target on a channel that is already open, without a close in
// between, as a client that sends LOAD_SU twice does. The first file stayed open.
static void s11_crash_uci_reopen(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-Crash-UciReopen";
    s11_partition(fm, dr, "ucireopen");
    expect_iec_write_ok(testname, dr, 1, "PROG", "program");
    IecChannel *channel = dr->get_data_channel(0);
    channel->ext_open_file("PROG"); // one file open, as there is after every open
    size_t before = s11_heap_in_use();
    for (int i = 0; i < 20; i++) {
        channel->ext_open_file("PROG");
    }
    size_t after = s11_heap_in_use();
    channel->ext_close_file();
    printf("%s: %d bytes in use with one open, %d after 20 more\n", testname, (int)before, (int)after);
    REQUIRE(after < before + 1024);
}

// A record positioned past its last byte: the channel offered a negative number of
// bytes, which the UCI target passes on as a length to copy (found by Suite11-Soak).
static void s11_crash_record_past_end(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-Crash-RecordPastEnd";
    s11_partition(fm, dr, "recordend");
    expect_rel_open(testname, dr, 2, "RECORDS", 50);
    expect_rel_position_status(testname, dr, 2, 1, 1, "50,RECORD NOT PRESENT,00,00\r");
    expect_rel_write(testname, dr, 2, (const uint8_t *)"AB", 2);
    expect_rel_position_status(testname, dr, 2, 1, 40, "00, OK,00,00\r");
    negative_fetches = 0;
    uint8_t out[64];
    int got = read_file_limited(dr, 2, out, sizeof(out));
    printf("%s: reading from byte 40 of a record that ends at byte 2 gave %d bytes, %d negative offers\n",
           testname, got, negative_fetches);
    REQUIRE(negative_fetches == 0);
    close_file(dr, 2);
}

// Every command of one and two bytes, and of each letter followed by the terminator,
// answers something: a parser that takes a substring past the end of a short command
// writes outside the heap (found by Suite11-Soak with "E").
static void s11_short_commands(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-ShortCommands";
    s11_partition(fm, dr, "short");
    for (int a = 0; a < 256; a++) {
        const uint8_t one[1] = { (uint8_t)a };
        send_command_data(dr, one, 1);
        get_status(dr);
        REQUIRE(isdigit((uint8_t)last_status[0]) && isdigit((uint8_t)last_status[1]));
        const uint8_t with_cr[2] = { (uint8_t)a, 0x0D };
        send_command_data(dr, with_cr, 2);
        get_status(dr);
        REQUIRE(isdigit((uint8_t)last_status[0]) && isdigit((uint8_t)last_status[1]));
        for (int b = 0; b < 256; b += (a >= 'A' && a <= 'Z') ? 1 : 51) {
            const uint8_t two[2] = { (uint8_t)a, (uint8_t)b };
            send_command_data(dr, two, 2);
            get_status(dr);
            REQUIRE(isdigit((uint8_t)last_status[0]) && isdigit((uint8_t)last_status[1]));
        }
    }
    expect_command_response(testname, dr, "E\r", "30,SYNTAX ERROR,00,00\r");
}

// ---------------------------------------------------------------------------
// Suite11-Soak: randomised use of the drive. Every iteration either follows the
// pattern C64 OS follows at boot (binary Change Partition, CD to absolute paths, M-R
// probes, UI, a scratch with a path, a listing abandoned after five bytes, several
// channels open at once, a file header read and then the whole file again), or runs
// one of the other command families, or sends hostile input: random command bytes,
// random names, channels abandoned half way, the menu's Reset and partition changes
// in between. Individual answers are not checked, only that there is one. What is
// checked is that the drive survives (run it in the AddressSanitizer build), that it
// still answers UI with 73 at the end, and that it gives back the memory it took.
// IECSOAK_ITERATIONS sets the length (default 400), IECSOAK_SEED the sequence, and
// IECSOAK_VERBOSE=1 prints each step so a failing seed can be followed.
// ---------------------------------------------------------------------------
static uint32_t soak_state = 1;
static bool soak_verbose = false;

static uint32_t soak_rand(void)
{
    soak_state ^= soak_state << 13;
    soak_state ^= soak_state >> 17;
    soak_state ^= soak_state << 5;
    return soak_state;
}

static int soak_pick(int n)
{
    return (int)(soak_rand() % (uint32_t)n);
}

static void soak_step(int iteration, const char *what, const char *detail)
{
    if (soak_verbose) {
        fprintf(stderr, "soak %d: %s %s\n", iteration, what, detail ? detail : "");
    }
}

static const char *soak_names[] = {
    "ONE", "TWO", "GAME", "CONFIG.T", "LIB.O", "A\xA0" "B", "_", "..", "X*", "Y?",
    "SIXTEENCHARSNAME", "SEVENTEENCHARNAME", "REL", "MY GAME", "IMG.D64", "",
};
#define SOAK_NAMES (int)(sizeof(soak_names) / sizeof(soak_names[0]))

static void soak_send_bytes(IecDrive *dr, uint8_t chan_cmd, const uint8_t *data, int len)
{
    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(chan_cmd);
    for (int i = 0; i < len; i++) {
        dr->push_data(data[i]);
    }
    dr->push_ctrl(SLAVE_CMD_EOI);
}

static void soak_command(IecDrive *dr, const char *cmd)
{
    send_command(dr, cmd);
}

static void soak_read(IecDrive *dr, uint8_t chan, int len)
{
    static uint8_t sink[4096];
    if (len > (int)sizeof(sink)) {
        len = sizeof(sink);
    }
    read_file_limited(dr, chan, sink, len);
}

static void soak_fixture(FileManager *fm)
{
    const char *testname = "Suite11-Soak";
    static const char *dirs[] = { "/Fat/soak", "/Fat/soak/OS", "/Fat/soak/OS/SETTINGS", "/Fat/soak/OS/LIBRARY",
                                  "/Fat/soak/OS/TEMPORARY", "/Fat/soak/OS/DRIVERS", "/Fat/soak/WORK" };
    for (int i = 0; i < 7; i++) {
        FRESULT fres = fm->create_dir(dirs[i]);
        REQUIRE((fres == FR_OK) || (fres == FR_EXIST));
    }
    uint8_t data[3000];
    for (int i = 0; i < (int)sizeof(data); i++) {
        data[i] = (uint8_t)(i * 13 + 7);
    }
    uint32_t tr;
    REQUIRE(fm->save_file(true, "/Fat/soak/OS/SETTINGS", "CONFIG.T.seq", data, 13, &tr) == FR_OK);
    REQUIRE(fm->save_file(true, "/Fat/soak/OS/SETTINGS", "SYSTEM.T.seq", data, 400, &tr) == FR_OK);
    REQUIRE(fm->save_file(true, "/Fat/soak/OS/LIBRARY", "LIB.O.prg", data, 2957, &tr) == FR_OK);
    REQUIRE(fm->save_file(true, "/Fat/soak/OS/LIBRARY", "SMALL.O.prg", data, 2, &tr) == FR_OK);
    REQUIRE(fm->save_file(true, "/Fat/soak/OS/DRIVERS", "KBD.C64.prg", data, 567, &tr) == FR_OK);
    s11_host_file(fm, "/Fat/soak/OS/LIBRARY", "GAME.P00", "MY GAME", 0, data, 300);
    create_formatted_image(fm, "/Fat/soak/OS/IMG.D64", "SOAK", 683, e_image_d64);
}

static void soak_partitions(FileManager *fm, IecDrive *dr)
{
    dr->add_partition(60, "/Fat/soak", "SOAK");
    create_formatted_image(fm, "/Fat/soak_61.d64", "SOAK41", 683, e_image_d64);
    create_formatted_image(fm, "/Fat/soak_62.d81", "SOAK81", 3200, e_image_d81);
    create_formatted_image(fm, "/Fat/soak_63.dnp", "SOAKNAT", 4 * 256, e_image_dnp);
    dr->add_partition(61, "/Fat/soak_61.d64", "SOAK41");
    dr->add_partition(62, "/Fat/soak_62.d81", "SOAK81");
    dr->add_partition(63, "/Fat/soak_63.dnp", "SOAKNAT");
}

// What C64 OS does at boot, from the trace attached to #877.
static void soak_boot(IecDrive *dr, int iteration)
{
    soak_step(iteration, "boot", NULL);
    const uint8_t cp[3] = { 'C', 0xD0, 60 };
    soak_send_bytes(dr, 0x6F, cp, 3);
    get_status(dr);
    soak_command(dr, "CD//OS\r");
    static const uint8_t probes[4][2] = { { 0xA4, 0xFE }, { 0xC5, 0xE5 }, { 0xE8, 0xA6 }, { 0x02, 0x00 } };
    for (int i = 0; i < 4; i++) {
        const uint8_t mr[6] = { 'M', '-', 'R', probes[i][0], probes[i][1], 2 };
        soak_send_bytes(dr, 0xFF, mr, 6);
        get_status(dr);
        close_file(dr, 15);
    }
    soak_command(dr, "UI");
    soak_command(dr, "S/TEMPORARY/:*");
    open_file(dr, 0, "$");
    get_status(dr);
    soak_read(dr, 0, 5);
    close_file(dr, 0);
    open_file(dr, 2, "/SETTINGS/:SYSTEM.T");
    get_status(dr);
    soak_read(dr, 2, 1 + soak_pick(40));
    open_file(dr, 3, "/LIBRARY/:LIB.O");
    get_status(dr);
    soak_read(dr, 3, 2);
    close_file(dr, 3);
    open_file(dr, 0, "/LIBRARY/:LIB.O");
    get_status(dr);
    soak_read(dr, 0, 4096);
    close_file(dr, 0);
    open_file(dr, 3, "/TEMPORARY/:UPDATER");
    get_status(dr);
    soak_read(dr, 3, 10);
    close_file(dr, 3);
    open_file(dr, 14, "/SETTINGS/:CONFIG.T");
    get_status(dr);
    soak_read(dr, 14, 16);
    close_file(dr, 14);
    soak_command(dr, "CD//OS/DRIVERS/");
    open_file(dr, 0, "KBD.C64");
    get_status(dr);
    soak_read(dr, 0, 4096);
    close_file(dr, 0);
    soak_command(dr, "CD//OS");
    open_file(dr, 0, "$:SYS*");
    get_status(dr);
    soak_read(dr, 0, 4096);
    close_file(dr, 0);
    soak_read(dr, 2, 4096);
    close_file(dr, 2);
}

static void soak_listing(IecDrive *dr, int iteration)
{
    static const char *names[] = { "$", "$:*", "$=P", "$=T:*=L", "$//OS/", "$0", "$/NOSUCH/", "$61", "$62:*=S",
                                   "$63", "$=P:*=N", "$//OS/IMG.D64/", "$:*=H" };
    const char *name = names[soak_pick(13)];
    uint8_t chan = (uint8_t)soak_pick(15);
    soak_step(iteration, "listing", name);
    open_file(dr, chan, name);
    get_status(dr);
    soak_read(dr, chan, soak_pick(2) ? 4096 : soak_pick(200));
    if (soak_pick(4)) {
        close_file(dr, chan);
    }
}

static void soak_work_dir(IecDrive *dr)
{
    const uint8_t cp[3] = { 'C', 0xD0, (uint8_t)(60 + soak_pick(4)) };
    soak_send_bytes(dr, 0x6F, cp, 3);
    get_status(dr);
    if (cp[2] == 60) {
        soak_command(dr, "CD//WORK/");
    } else {
        soak_command(dr, "CD//");
    }
}

static void soak_write(IecDrive *dr, int iteration)
{
    char name[48];
    static const char *modes[] = { ",S,W", ",P,W", ",U,W", ",S,A", "" };
    const char *base = soak_names[soak_pick(SOAK_NAMES)];
    snprintf(name, sizeof(name), "%s%s%s", soak_pick(3) ? "" : "@:", base, modes[soak_pick(5)]);
    uint8_t chan = (uint8_t)(1 + soak_pick(14));
    soak_step(iteration, "write", name);
    soak_work_dir(dr);
    open_file(dr, chan, name);
    get_status(dr);
    uint8_t data[1600];
    int len = soak_pick(sizeof(data));
    for (int i = 0; i < len; i++) {
        data[i] = (uint8_t)soak_rand();
    }
    soak_send_bytes(dr, 0x60 | chan, data, len);
    if (soak_pick(6)) {
        close_file(dr, chan);
    }
    open_file(dr, chan, base);
    get_status(dr);
    soak_read(dr, chan, 4096);
    close_file(dr, chan);
}

static void soak_files(IecDrive *dr, int iteration)
{
    char cmd[80];
    const char *a = soak_names[soak_pick(SOAK_NAMES)];
    const char *b = soak_names[soak_pick(SOAK_NAMES)];
    switch (soak_pick(9)) {
    case 0: snprintf(cmd, sizeof(cmd), "S:%s", a); break;
    case 1: snprintf(cmd, sizeof(cmd), "R:%s=%s", a, b); break;
    case 2: snprintf(cmd, sizeof(cmd), "C:%s=%s", a, b); break;
    case 3: snprintf(cmd, sizeof(cmd), "MD:%s", a); break;
    case 4: snprintf(cmd, sizeof(cmd), "CD:%s", a); break;
    case 5: snprintf(cmd, sizeof(cmd), "RD:%s", a); break;
    case 6: snprintf(cmd, sizeof(cmd), "L:%s", a); break;
    case 7: snprintf(cmd, sizeof(cmd), "S:*"); break;
    default: snprintf(cmd, sizeof(cmd), "CD_"); break;
    }
    soak_step(iteration, "files", cmd);
    soak_work_dir(dr);
    soak_command(dr, cmd);
}

static void soak_relative(IecDrive *dr, int iteration)
{
    soak_step(iteration, "relative", NULL);
    soak_work_dir(dr);
    char name[16];
    int n = snprintf(name, sizeof(name), "REL,L,");
    name[n++] = (char)(1 + soak_pick(254));
    name[n] = 0;
    uint8_t chan = (uint8_t)(2 + soak_pick(12));
    open_file(dr, chan, soak_pick(3) ? name : "REL,L");
    get_status(dr);
    for (int i = soak_pick(5); i >= 0; i--) {
        int record = 1 + soak_pick(soak_pick(4) ? 20 : 2000);
        const uint8_t p[5] = { 'P', (uint8_t)(0x60 | chan), (uint8_t)record, (uint8_t)(record >> 8), (uint8_t)soak_pick(256) };
        soak_send_bytes(dr, 0x6F, p, 5);
        get_status(dr);
        if (soak_pick(2)) {
            uint8_t data[300];
            int len = soak_pick(sizeof(data));
            for (int k = 0; k < len; k++) {
                data[k] = (uint8_t)soak_rand();
            }
            soak_send_bytes(dr, 0x60 | chan, data, len);
        } else {
            soak_read(dr, chan, soak_pick(300));
        }
    }
    close_file(dr, chan);
}

static void soak_direct(IecDrive *dr, int iteration)
{
    soak_step(iteration, "direct", NULL);
    soak_work_dir(dr);
    uint8_t chan = (uint8_t)(2 + soak_pick(12));
    char name[4] = { '#', '#', (char)('0' + soak_pick(11)), 0 };
    open_file(dr, chan, soak_pick(2) ? "#" : name);
    get_status(dr);
    static const char *forms[] = { "U1:%d,0,%d,%d", "U2:%d,0,%d,%d", "B-R:%d,0,%d,%d", "B-W:%d,0,%d,%d",
                                   "B-P:%d,%d,%d", "B-A:0,%d,%d%.0d", "B-F:0,%d,%d%.0d" };
    for (int i = soak_pick(6); i >= 0; i--) {
        char cmd[48];
        int f = soak_pick(7);
        if (f >= 5) {
            snprintf(cmd, sizeof(cmd), forms[f], soak_pick(90), soak_pick(260), 0);
        } else {
            snprintf(cmd, sizeof(cmd), forms[f], soak_pick(3) ? chan : soak_pick(16), soak_pick(90), soak_pick(260));
        }
        soak_command(dr, cmd);
        if (soak_pick(2)) {
            uint8_t data[600];
            int len = soak_pick(sizeof(data));
            for (int k = 0; k < len; k++) {
                data[k] = (uint8_t)soak_rand();
            }
            soak_send_bytes(dr, 0x60 | chan, data, len);
        } else {
            soak_read(dr, chan, soak_pick(2400));
        }
    }
    close_file(dr, chan);
}

static void soak_hostile(IecDrive *dr, int iteration)
{
    uint8_t data[300];
    int len = soak_pick(sizeof(data));
    for (int i = 0; i < len; i++) {
        data[i] = soak_pick(3) ? (uint8_t)(' ' + soak_pick(64)) : (uint8_t)soak_rand();
    }
    if (soak_pick(2)) {
        soak_step(iteration, "hostile command", NULL);
        soak_send_bytes(dr, 0x6F, data, len);
        get_status(dr);
    } else {
        uint8_t chan = (uint8_t)soak_pick(16);
        soak_step(iteration, "hostile open", NULL);
        soak_send_bytes(dr, 0xF0 | chan, data, len);
        get_status(dr);
        soak_read(dr, chan, soak_pick(600));
        if (soak_pick(2)) {
            soak_send_bytes(dr, 0x60 | chan, data, soak_pick(len + 1));
        }
        if (soak_pick(3)) {
            close_file(dr, chan);
        }
    }
}

static void soak_misc(IecDrive *dr, int iteration)
{
    static const char *cmds[] = { "G-P", "G-P\x01", "T-RI", "T-RA", "T-WI2026-09-13T01:02:03", "I", "UJ",
                                  "U\xCA", "L:ONE", "XPWD", "M-R\x00\x05\xFF", "M-W\x00\x05\x01\x01", "N:TMP.D64,AB",
                                  "S:TMP.D64", "CP61", "CP99", "CD//OS/IMG.D64", "CD_", "U0>\x0B" };
    const char *cmd = cmds[soak_pick(sizeof(cmds) / sizeof(cmds[0]))];
    soak_step(iteration, "misc", cmd);
    if (strncmp(cmd, "N:", 2) == 0) {
        soak_work_dir(dr);
    }
    if (strncmp(cmd, "M-", 2) == 0) {
        soak_send_bytes(dr, 0x6F, (const uint8_t *)cmd, (cmd[2] == 'R') ? 6 : 8);
        get_status(dr);
    } else {
        soak_command(dr, cmd);
    }
}

static void soak_gui(FileManager *fm, IecDrive *dr, int iteration)
{
    switch (soak_pick(4)) {
    case 0:
        soak_step(iteration, "gui", "reset");
        dr->reset();
        break;
    case 1: {
        soak_step(iteration, "gui", "partition moves");
        dr->add_partition(60, soak_pick(2) ? "/Fat/soak" : "/Fat/soak/OS", "SOAK");
        break;
    }
    case 2: {
        soak_step(iteration, "gui", "info");
        static char text[4096];
        StreamTextLog log(sizeof(text), text);
        dr->info(log);
        break;
    }
    default:
        soak_step(iteration, "gui", "close channels");
        for (int c = 0; c < 16; c++) {
            close_file(dr, (uint8_t)c);
        }
        break;
    }
}

static void s11_soak(FileManager *fm, IecDrive *dr)
{
    const char *testname = "Suite11-Soak";
    const char *env = getenv("IECSOAK_ITERATIONS");
    int iterations = env ? atoi(env) : 400;
    env = getenv("IECSOAK_SEED");
    soak_state = env ? (uint32_t)strtoul(env, NULL, 0) : 0x1541;
    if (!soak_state) {
        soak_state = 1;
    }
    soak_verbose = getenv("IECSOAK_VERBOSE") != NULL;
    // With IECSOAK_LOG set, the drive also writes its "Log Every Operation" lines.
    s11_softiec_settings()->set_value(0x55, getenv("IECSOAK_LOG") ? 1 : 0);
    printf("%s: %d iterations, seed 0x%x\n", testname, iterations, soak_state);
    soak_fixture(fm);
    soak_partitions(fm, dr);

    size_t heap_after_warmup = 0;
    int warmup = iterations / 5;
    for (int i = 0; i < iterations; i++) {
        switch (soak_pick(16)) {
        case 0: case 1: case 2: case 3: soak_boot(dr, i); break;
        case 4: case 5: soak_listing(dr, i); break;
        case 6: case 7: soak_write(dr, i); break;
        case 8: soak_files(dr, i); break;
        case 9: soak_relative(dr, i); break;
        case 10: soak_direct(dr, i); break;
        case 11: case 12: soak_hostile(dr, i); break;
        case 13: soak_misc(dr, i); break;
        default: soak_gui(fm, dr, i); break;
        }
        if (i == warmup) {
            for (int c = 0; c < 16; c++) {
                close_file(dr, (uint8_t)c);
            }
            heap_after_warmup = s11_heap_in_use();
        }
    }
    for (int c = 0; c < 16; c++) {
        close_file(dr, (uint8_t)c);
    }
    size_t heap_at_end = s11_heap_in_use();
    const char *ui = send_command(dr, "UI");
    printf("%s: after %d iterations UI answered %s", testname, iterations, ui);
    printf("%s: heap in use %d bytes after warmup, %d at the end\n", testname,
           (int)heap_after_warmup, (int)heap_at_end);
    printf("%s: %d reads were offered a negative number of bytes\n", testname, negative_fetches);
    REQUIRE(strncmp(ui, "73,", 3) == 0);
    REQUIRE(negative_fetches == 0);
    REQUIRE(heap_at_end <= heap_after_warmup + 64 * 1024);
}


struct Suite11Case {
    const char *name;
    void (*run)(FileManager *fm, IecDrive *dr);
};

static const Suite11Case suite11_cases[] = {
    { "Suite11-SI031-Unrecognised",      s11_si031_unrecognised },
    { "Suite11-SI030-UnknownSubcommand", s11_si030_unknown_subcommand },
    { "Suite11-SI030-MissingName",       s11_si030_missing_name },
    { "Suite11-SI030-WildcardTarget",    s11_si030_wildcard_target },
    { "Suite11-SI036-BlockRange",        s11_si036_block_range },
    { "Suite11-SI033-ScratchNothing",    s11_si033_scratch_nothing },
    { "Suite11-SI053-Initialize",        s11_si053_initialize },
    { "Suite11-SI045-PartitionDirectory", s11_si045_partition_directory },
    { "Suite11-SI046-PartitionCount",    s11_si046_partition_count },
    { "Suite11-SI100-DeviceNumber",      s11_si100_device_number },
    { "Suite11-SI105-MemoryCommands",    s11_si105_memory_commands },
    { "Suite11-SI021-LongNames",         s11_si021_long_names },
    { "Suite11-SI022-TooLong",           s11_si022_too_long },
    { "Suite11-SI016-SecondTerminator",  s11_si016_second_terminator },
    { "Suite11-SI018-PositionExempt",    s11_si018_position_exempt },
    { "Suite11-SI014-LeftArrow",         s11_si014_left_arrow },
    { "Suite11-SI012-WildcardPath",      s11_si012_wildcard_path },
    { "Suite11-SI060-MdColon",           s11_si060_md_colon },
    { "Suite11-SI063-RdNoPath",          s11_si063_rd_no_path },
    { "Suite11-SI150-DeepPath",          s11_si150_deep_path },
    { "Suite11-SI130-ListingHeader",     s11_si130_listing_header },
    { "Suite11-SI133-SizeRemainder",     s11_si133_size_remainder },
    { "Suite11-SI138-ListingEof",       s11_si138_listing_eof },
    { "Suite11-SI139-StampedEntries",    s11_si139_stamped_entries },
    { "Suite11-SI149-GeosEntries",       s11_si149_geos_entries },
    { "Suite11-SI134-HiddenFlag",        s11_si134_hidden_flag },
    { "Suite11-SI065-HeaderName",        s11_si065_header_name },
    { "Suite11-SI032-WildcardWrite",     s11_si032_wildcard_write },
    { "Suite11-SI041-PartitionSize",     s11_si041_partition_size },
    { "Suite11-SI072-RawNames",          s11_si072_raw_names },
    { "Suite11-SI074-RenameChecks",      s11_si074_rename_checks },
    { "Suite11-SI083-SeekWrite",         s11_si083_seek_write },
    { "Suite11-SI083-SeekWriteImage",    s11_si083_seek_write_image },
    { "Suite11-SI071-Format",            s11_si071_format },
    { "Suite11-SI147-ShiftedSpace",      s11_si147_shifted_space },
    { "Suite11-SI142-EscapedWildcards",  s11_si142_escaped_wildcards },
    { "Suite11-SI076-Lock",              s11_si076_lock },
    { "Suite11-SI077-AttributeCommands", s11_si077_attribute_commands },
    { "Suite11-SI051-RenamePartition",   s11_si051_rename_partition },
    { "Suite11-SI064-RenameHeader",      s11_si064_rename_header },
    { "Suite11-SI102-WriteProtect",      s11_si102_write_protect },
    { "Suite11-SI090-BufferPointer",     s11_si090_buffer_pointer },
    { "Suite11-SI093-BoundPartition",    s11_si093_bound_partition },
    { "Suite11-SI094-BlockLength",       s11_si094_block_length },
    { "Suite11-SI103-Resets",            s11_si103_resets },
    { "Suite11-SI144-ReadX00",           s11_si144_read_x00 },
    { "Suite11-SI144-SharedHeader",      s11_si144_shared_header },
    { "Suite11-SI132-Splat",             s11_si132_splat },
    { "Suite11-DeliberateExclusions",    s11_deliberate_exclusions },
    { "Suite11-SI084-RelLayouts",        s11_si084_rel_layouts },
    { "Suite11-SI144-X00Paths",          s11_x00_paths },
    { "Suite11-SI084-RelInImage",        s11_rel_in_image },
    { "Suite11-CR4-AbandonedListing",    s11_cr4_abandoned_listing },
    { "Suite11-CR5-FailedDirectoryOpen", s11_cr5_failed_directory_open },
    { "Suite11-CR6-Lock",                s11_cr6_lock },
    { "Suite11-CR8-ScratchScan",         s11_cr8_scratch_scan },
    { "Suite11-FailureLog",              s11_failure_log },
    { "Suite11-OperationLog",            s11_operation_log },
    { "Suite11-OperationLogBounds",      s11_operation_log_bounds },
    { "Suite11-OperationLogNoReconfigure", s11_operation_log_no_reconfigure },
    { "Suite11-BlockAllocateAnswers",    s11_block_allocate_answers },
    { "Suite11-Crash-DamagedChain",      s11_crash_damaged_chain },
    { "Suite11-Crash-LongHostName",      s11_crash_long_host_name },
    { "Suite11-Crash-RecordPastEnd",     s11_crash_record_past_end },
    { "Suite11-Crash-SeekPastEndImage",  s11_crash_seek_past_end_image },
    { "Suite11-Crash-EvictOpenListing",  s11_crash_evict_open_listing },
    { "Suite11-Crash-CopyMissingPartition", s11_crash_copy_missing_partition },
    { "Suite11-Crash-UciReopen",         s11_crash_uci_reopen },
    { "Suite11-ShortCommands",           s11_short_commands },
    { "Suite11-Soak",                    s11_soak },
};

// Runs every case, or only those whose name contains `only`.
void execute_suite11(FileManager *fm, IecDrive *dr, const char *only)
{
    const char *testname = "Suite11";
    int ran = 0;
    print_scenario("Suite11", "Software IEC compatibility specification");
    for (size_t i = 0; i < sizeof(suite11_cases) / sizeof(suite11_cases[0]); i++) {
        if (only && !strstr(suite11_cases[i].name, only)) {
            continue;
        }
        printf("  %s\n", suite11_cases[i].name);
        suite11_cases[i].run(fm, dr);
        ran++;
    }
    if (only) {
        printf("Suite11 ran %d case(s) matching '%s'\n", ran, only);
        REQUIRE(ran > 0);
        return;
    }
    printf("Suite11 completed successfully!\n");
}


int main(int argc, const char **argv)
{
    UserInterface *ui = new UserInterface("Test Drive");

    init_ram_disk();
    init_flash_disk();
    init_fat_file();

    IecDrive *dr = new IecDrive();
    t_channel_retval ret;
    uint32_t tr;
    mstring status;

    File *f;
    FileManager *fm = FileManager :: getFileManager();

    // With an argument, only the Suite11 cases whose name contains it run.
    const char *only = (argc > 1) ? argv[1] : NULL;
    if (!only) {
        execute_suite8(fm, dr);
        run_suite9_block_matrix(fm, dr);
        execute_suite3(fm, dr);
        execute_suite4(fm, dr);
        execute_suite5(fm, dr);
        execute_suite6(fm, dr);
        execute_suite7(fm, dr);
        execute_suite10(fm, dr);
        execute_suite12(fm);
    }
    execute_suite11(fm, dr, only);

    delete dr;
    delete ui;
    //delete fm;
    return 0;
}
