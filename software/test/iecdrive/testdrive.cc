#include "iec_drive.h"
#include "dump_hex.h"
#include "filemanager.h"
#include "file_device.h"
#include "filesystem_fat.h"
#include "macros.h"

void outbyte(int c) { putc(c, stdout); }
CommandInterface cmd_if;

BlockDevice *ramdisk_blk;
FileDevice *ramdisk_node;

BlockDevice *flashdisk_blk;
FileDevice *flashdisk_node;

char last_status[128];
int last_status_size;
void create_iec_d64_fixture(const char *path);
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

static void capture_block_sector(const char *testname, IecDrive *dr, uint8_t chan,
                                 int part, int track, int sector, uint8_t *out)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "B-R %u %d %d %d", chan, part, track, sector);
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
    snprintf(cmd, sizeof(cmd), "B-R %u %d %d %d", chan, part, track, sector);
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
    snprintf(cmd, sizeof(cmd), "B-W %u %d %d %d", chan, part, track, sector);
    expect_command_ok(testname, dr, cmd);
    uint8_t actual[256];
    snprintf(cmd, sizeof(cmd), "B-R %u %d %d %d", chan, part, track, sector);
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
    snprintf(cmd, sizeof(cmd), "B-R %u %d %d %d", chan, c.partition, c.bam_track, c.bam_sector);
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
void init_fat_file()
{
    create_file("format.fat", 4*1*1024);
    static BlockDevice_Emulated blk("format.fat", 512);
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
    expect_command_response("TEST16", dr, "C3:BAD=", "32,SYNTAX ERROR,00,00\r");

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
    expect_command_ok("Suite6-RDRenderedParentPath", dr, "RD6/PARENT:CHILD");
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
    expect_iec_open_status_prefix("Suite7-OpenMalformedReplace", dr, 0, "@345:", "32,SYNTAX ERROR");
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
    expect_command_ok("Suite8-CD-PARENT-OTHERDIR", dr, "CD/_/OTHERDIR");
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

static void run_suite8_time_copy_rename_scratch(IecDrive *dr)
{
    const char *suite = "Suite8";
    const char *msg = (const char *)testmsg;
    const char *write_msg = "This is some random string that should be written to an open file.";

    print_scenario(suite, "Time, copy, rename, scratch");
    expect_command_response("Suite8-T-RA", dr, "T-RA", "WED. 26/06/25 12:41:01 AM\r");
    expect_command_response("Suite8-T-RI", dr, "T-RI", "2025-06-26T00:41:01 WED\r");
    static const uint8_t t_rd[] = { 3, 125, 6, 26, 12, 41, 1, 0, 0x0d };
    static const uint8_t t_rb[] = { 3, 0x25, 0x06, 0x26, 0x12, 0x41, 0x01, 0, 0x0d };
    expect_command_bytes("Suite8-T-RD", dr, "T-RD", t_rd, sizeof(t_rd));
    expect_command_bytes("Suite8-T-RB", dr, "T-RB", t_rb, sizeof(t_rb));

    expect_command_response("Suite8-COPY-MISSING-SOURCE", dr, "C2:DEST=", "32,SYNTAX ERROR,00,00\r");
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
    expect_directory_read("Suite8-DIR-PARENT", dr, "$_");
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

        uint8_t expected_read[256];
        make_sector_payload(expected_read, "BASIC:PRG");
        open_buffer_channel("Suite9-OpenBufferRead", dr, 2);
        capture_block_sector("Suite9-BlockRead", dr, 2, c.partition, c.read_track, c.read_sector, expected_read);
        close_file(dr, 2);
        expect_status_ok("Suite9-CloseBufferRead", "#2");

        uint8_t write_payload[256];
        make_increment_pattern(write_payload);
        open_buffer_channel("Suite9-OpenBufferWrite", dr, 2);
        send_channel_data(dr, 2, write_payload, sizeof(write_payload));
        expect_block_write_sector("Suite9-BlockWrite", dr, 2, c.partition, c.write_track, c.write_sector, write_payload);
        close_file(dr, 2);
        expect_status_ok("Suite9-CloseBufferWrite", "#2");

        uint32_t free_before = get_free_sectors(fm, c.mount);

        expect_command_ok("Suite9-BlockAllocate", dr, "B-A 2 9 17 10");
        uint32_t free_after_alloc = get_free_sectors(fm, c.mount);
        REQUIRE(free_after_alloc + 1 == free_before);

        expect_command_ok("Suite9-BlockFree", dr, "B-F 2 9 17 10");
        uint32_t free_after_free = get_free_sectors(fm, c.mount);
        REQUIRE(free_after_free == free_before);
    }

    print_scenario(suite, "Block commands on FAT fail");
    prepare_fat_partition(fm, dr, fat_path, 8, "FAT-BLOCK");
    expect_command_status_prefix("Suite9-FAT-BlockRead", dr, "B-R 2 8 17 0", "78,BLOCK ACCESS DENIED");
    expect_command_status_prefix("Suite9-FAT-BlockWrite", dr, "B-W 2 8 17 0", "78,BLOCK ACCESS DENIED");
    expect_command_status_prefix("Suite9-FAT-BlockAllocate", dr, "B-A 2 8 17 0", "78,BLOCK ACCESS DENIED");
    expect_command_status_prefix("Suite9-FAT-BlockFree", dr, "B-F 2 8 17 0", "78,BLOCK ACCESS DENIED");

    printf("Suite9 completed successfully!\n");
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

    execute_suite8(fm, dr);
    run_suite9_block_matrix(fm, dr);
    execute_suite3(fm, dr);
    execute_suite4(fm, dr);
    execute_suite5(fm, dr);
    execute_suite6(fm, dr);
    execute_suite7(fm, dr);

    delete dr;
    delete ui;
    //delete fm;
    return 0;
}
