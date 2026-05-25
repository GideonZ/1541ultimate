#include "iec_drive.h"
#include "dump_hex.h"
#include "filemanager.h"
#include "file_device.h"
#include "filesystem_fat.h"
#include <assert.h>

#define REQUIRE(expr) do { \
    bool require_ok = (expr); \
    if (!require_ok) { \
        fprintf(stderr, "ASSERT FAILED %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    } \
    assert(require_ok); \
} while (0)

void outbyte(int c) { putc(c, stdout); }
CommandInterface cmd_if;

BlockDevice *ramdisk_blk;
FileDevice *ramdisk_node;

char last_status[128];

static const int D64_SIZE = 174848;
static const int D64_TRACKS = 35;
static const int D64_SECTORS_PER_TRACK[D64_TRACKS + 1] = {
    0,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    19, 19, 19, 19, 19, 19, 19,
    18, 18, 18, 18, 18, 18,
    17, 17, 17, 17, 17
};

static int d64_abs_sector(int track, int sector)
{
    REQUIRE(track >= 1);
    REQUIRE(track <= D64_TRACKS);
    REQUIRE(sector >= 0);
    REQUIRE(sector < D64_SECTORS_PER_TRACK[track]);

    int abs = sector;
    for (int t = 1; t < track; t++) {
        abs += D64_SECTORS_PER_TRACK[t];
    }
    return abs;
}

static uint8_t *d64_sector(uint8_t *disk, int track, int sector)
{
    return disk + 256 * d64_abs_sector(track, sector);
}

static void d64_set_allocated(uint8_t *bam, int track, int sector)
{
    uint8_t *entry = bam + 4 * track;
    uint8_t bit = (uint8_t)(1 << (sector & 7));
    if (entry[1 + (sector >> 3)] & bit) {
        entry[1 + (sector >> 3)] &= (uint8_t)~bit;
        entry[0]--;
    }
}

static void d64_add_file(uint8_t *disk, int dir_index, const uint8_t *name, int name_len,
                         uint8_t type, int track, int sector, const char *payload)
{
    REQUIRE(dir_index >= 0);
    REQUIRE(dir_index < 8);
    REQUIRE(name_len > 0);
    REQUIRE(name_len <= 16);

    uint8_t *dir = d64_sector(disk, 18, 1);
    uint8_t *entry = dir + 32 * dir_index;
    entry[2] = (uint8_t)(0x80 | type);
    entry[3] = (uint8_t)track;
    entry[4] = (uint8_t)sector;
    memset(entry + 5, 0xA0, 16);
    memcpy(entry + 5, name, name_len);
    entry[30] = 1;
    entry[31] = 0;

    int payload_len = strlen(payload);
    REQUIRE(payload_len > 0);
    REQUIRE(payload_len <= 254);

    uint8_t *data = d64_sector(disk, track, sector);
    data[0] = 0;
    data[1] = (uint8_t)(payload_len + 1);
    memcpy(data + 2, payload, payload_len);

    d64_set_allocated(d64_sector(disk, 18, 0), track, sector);
}

static void create_iec_d64_fixture(const char *path)
{
    uint8_t *disk = new uint8_t[D64_SIZE];
    memset(disk, 0, D64_SIZE);

    uint8_t *bam = d64_sector(disk, 18, 0);
    bam[0] = 18;
    bam[1] = 1;
    bam[2] = 0x41;
    bam[3] = 0x00;
    for (int track = 1; track <= D64_TRACKS; track++) {
        uint8_t *entry = bam + 4 * track;
        entry[0] = (uint8_t)D64_SECTORS_PER_TRACK[track];
        entry[1] = entry[2] = entry[3] = 0;
        for (int sector = 0; sector < D64_SECTORS_PER_TRACK[track]; sector++) {
            entry[1 + (sector >> 3)] |= (uint8_t)(1 << (sector & 7));
        }
    }
    memset(bam + 144, 0xA0, 27);
    memcpy(bam + 144, "IEC TEST", 8);
    bam[144 + 21] = '2';
    bam[144 + 22] = 'A';
    d64_set_allocated(bam, 18, 0);
    d64_set_allocated(bam, 18, 1);

    uint8_t *dir = d64_sector(disk, 18, 1);
    dir[0] = 0;
    dir[1] = 0xFF;

    static const uint8_t basic[] = "BASIC";
    static const uint8_t literal[] = "LITERAL";
    static const uint8_t literal_prg[] = "LITERAL.PRG";
    static const uint8_t onlybase[] = "ONLYBASE";
    static const uint8_t nasty[] = {
        'Q', '.', 0xC6, 0xC3, 0xC4, 0xC3, 0xC6, 0xC3,
        0xC4, 0xC3, 0xC6, 0xC3, 0xC4, 0xC3, 0xC6, 0xC3
    };

    d64_add_file(disk, 0, basic, sizeof(basic) - 1, 2, 17, 0, "BASIC:PRG");
    d64_add_file(disk, 1, basic, sizeof(basic) - 1, 1, 17, 1, "BASIC:SEQ");
    d64_add_file(disk, 2, literal, sizeof(literal) - 1, 2, 17, 2, "LITERAL:PRG");
    d64_add_file(disk, 3, literal_prg, sizeof(literal_prg) - 1, 2, 17, 3, "LITERAL.PRG:PRG");
    d64_add_file(disk, 4, literal_prg, sizeof(literal_prg) - 1, 1, 17, 4, "LITERAL.PRG:SEQ");
    d64_add_file(disk, 5, onlybase, sizeof(onlybase) - 1, 2, 17, 5, "ONLYBASE:PRG");
    d64_add_file(disk, 6, nasty, sizeof(nasty), 2, 17, 6, "NASTY:PRG");

    FILE *f = fopen(path, "wb");
    REQUIRE(f != NULL);
    REQUIRE(fwrite(disk, 1, D64_SIZE, f) == D64_SIZE);
    REQUIRE(fclose(f) == 0);
    delete[] disk;
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
    const int size = 1024 * 1024;
    const int sectors = size / sz;
    uint8_t *ramdisk_mem = new uint8_t[size]; // 1 MB only
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
    uint8_t *ramdisk_mem = new uint8_t[size]; // 1 MB only
    ramdisk_blk = new BlockDevice_Ram(ramdisk_mem, sz, sectors);
    {
        FileSystem *ramdisk_fs;
        Partition *ramdisk_prt;
        ramdisk_prt = new Partition(ramdisk_blk, 0, 0, 0);
        ramdisk_fs  = new FileSystemFAT(ramdisk_prt);
        ramdisk_fs->format("FlashDisk");
        delete ramdisk_fs;
        delete ramdisk_prt;
    }
    ramdisk_node = new FileDevice(ramdisk_blk, "Flash", "Internal Flash");
    ramdisk_node->attach_disk(sz);
    FileManager :: getFileManager()->add_root_entry(ramdisk_node);
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
    last_status[data_size] = 0;
    data[data_size] = 0;
    printf("Status: %s\n", data);
    //dump_hex_relative(data, data_size);
    dr->pop_more(data_size);
}

void get_status2(IecDrive *dr)
{
    uint8_t byte;
    t_channel_retval ret1, ret2;

    dr->push_ctrl(SLAVE_CMD_ATN);
    dr->push_ctrl(0x6f); // channel 15
    dr->talk();
    do {
        ret1 = dr->prefetch_data(byte);
        ret2 = dr->pop_data();
        printf("(%d) %02x (%d)\n", ret1, byte, ret2);
    } while(ret1 == IEC_OK);
    printf(".\n");
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
        printf("Ret: %d Count = %d\n", ret, data_size);
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

static void expect_status_ok(const char *context)
{
    if (strcmp(last_status, "00, OK,00,00\r") != 0) {
        fprintf(stderr, "%s: status was '%s'\n", context, last_status);
    }
    REQUIRE(strcmp(last_status, "00, OK,00,00\r") == 0);
}

static void expect_file_not_found(const char *context)
{
    if (strncmp(last_status, "62,FILE NOT FOUND", 17) != 0) {
        fprintf(stderr, "%s: status was '%s'\n", context, last_status);
    }
    REQUIRE(strncmp(last_status, "62,FILE NOT FOUND", 17) == 0);
}

static void expect_iec_file(IecDrive *dr, uint8_t chan, const char *name, const char *expected)
{
    uint8_t buffer[64];
    memset(buffer, 0, sizeof(buffer));

    open_file(dr, chan, name);
    get_status(dr);
    expect_status_ok(name);

    int got = read_file(dr, chan, buffer, sizeof(buffer));
    int expected_len = strlen(expected);
    if (got != expected_len || memcmp(buffer, expected, expected_len) != 0) {
        fprintf(stderr, "%s on channel %u: got %d bytes, expected %d bytes '%s'\n",
                name, chan, got, expected_len, expected);
    }
    REQUIRE(got == expected_len);
    REQUIRE(memcmp(buffer, expected, expected_len) == 0);

    close_file(dr, chan);
    expect_status_ok(name);
}

static void expect_iec_file_missing(IecDrive *dr, uint8_t chan, const char *name)
{
    open_file(dr, chan, name);
    get_status(dr);
    expect_file_not_found(name);
    close_file(dr, chan);
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
        printf("Ret: %d Count = %d\n", ret, data_size);
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

int execute_suite1(FileManager *fm, IecDrive *dr)
{
    mstring status;
    int error = 0;
    FRESULT fres;
    uint32_t tr;

    fm->create_dir("/Temp/Partition2");
    dr->add_partition(1, "/Temp", "RAMDISK");
    dr->add_partition(2, "/Temp/Partition2", "PART2");

    status = send_command(dr, "CP1");
    if(status != "02,PARTITION SELECTED,01,00\r") error++;

    status = send_command(dr, "CD/TEMP");
    if(status != "71,DIRECTORY ERROR,01,00\r") error++;

    status = send_command(dr, "CD/SOMEDIR");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CD/_/OTHERDIR");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CD:LEVEL2");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CD//");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CD/OTHERDIR/LEVEL2");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CD//:BLAH\xc1\xc2");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CD_");
    if(status != "00, OK,00,00\r") error++;

    read_directory(dr, "$=T0:*=L");

    // send_command(dr, "MD//HOI");
    status = send_command(dr, "MD:HOI");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "MD/OTHERDIR:DEEPER");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "MD_/OTHERDIR:DEEPER");
    if(status != "71,DIRECTORY ERROR,00,00\r") error++;
    status = send_command(dr, "MD:_/DEEPER");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "RD:HOI");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "MD:HOI");
    if(status != "00, OK,00,00\r") error++;

    fres = fm->save_file(false, "/Temp/HOI", "mmmmmmmmmmmmm.prg", testmsg, 28, &tr);
    if (fres == FR_OK) {
        printf("Written %u bytes to the file.\n", tr);
    } else {
        printf("%s\n", FileSystem::get_error_string(fres));
    }
    status = send_command(dr, "RD:HOI");
    if(status != "63,FILE EXISTS,00,00\r") error++;

    // read_directory(dr, "$");
    status = send_command(dr, "T-RA");
    if(status != "WED. 26/06/25 12:41:01 AM\r") error++;
    status = send_command(dr, "T-RI");
    if(status != "2025-06-26T00:41:01 WED\r") error++;
    status = send_command(dr, "T-RB");

    status = send_command(dr, "C2:DEST=");
    if(status != "32,SYNTAX ERROR,00,00\r") error++;
    status = send_command(dr, "C2:DEST=1:A,1:BB");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "CP2");
    if(status != "02,PARTITION SELECTED,02,00\r") error++;

    status = send_command(dr, "RENAME1:DOOM=1:DDDD");
    if(status != "00, OK,00,00\r") error++;

    status = send_command(dr, "RENAME2:DOOM=1:DOOM");
    if(status != "00, OK,00,00\r") error++;

    printf("\n\nError until now: %d\n\n", error);

    // send_command(dr, "CP3");
    read_directory(dr, "$1");
    // send_command(dr, "CP2");
    //read_directory(dr, "$=P");
    status = send_command(dr, "SCRATCH1:C*");
    if(status != "01, FILES SCRATCHED,03,00\r") error++;

    read_directory(dr, "1:A");

    write_file(dr, 3, "2:WRITETEST,U,W", "This is some random string that should be written to an open file.");

    fm->print_directory("/Temp/Partition2");
    return error;
}

int execute_suite2(FileManager *fm, IecDrive *dr)
{
    mstring status;
    int error = 0;
    uint32_t tr;

    read_directory(dr, "$=P");
    status = send_command(dr, "C\xD0\x0B");
    if(status != "02,PARTITION SELECTED,11,00\r") error++;

    fm->save_file(false, "/Temp", "ccc.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "cc2.prg", testmsg, 28, &tr);
    fm->save_file(false, "/Temp", "abc.prg", testmsg, 28, &tr);
    fm->create_dir("/Temp/Subdir");
    fm->save_file(false, "/Temp/SubDir", "01234.usr", testmsg, 28, &tr);
    read_directory(dr, "$:*=B");
    read_directory(dr, "$/Subdir");
    status = send_command(dr, "CD/Subdir");
    if(status != "00, OK,00,00\r") error++;
    status = send_command(dr, "XPWD");
    if(status != "/Subdir/") error++;
    read_directory(dr, "$//");
    read_directory(dr, "$_");

    return error;
}

int execute_suite3(FileManager *fm, IecDrive *dr)
{
    const char *diskname = "output/iec_corner_cases.d64";
    create_iec_d64_fixture(diskname);
    REQUIRE(copy_to(diskname, "/Temp/iec_corner_cases.d64") == FR_OK);
    dr->add_partition(1, "/Temp", "RAMDISK");
    dr->add_partition(3, "/Temp/iec_corner_cases.d64", "D64");

    expect_iec_file(dr, 0, "3:BASIC", "BASIC:PRG");
    expect_iec_file(dr, 2, "3:BASIC,P,R", "BASIC:PRG");
    expect_iec_file(dr, 2, "3:BASIC,S,R", "BASIC:SEQ");

    expect_iec_file(dr, 0, "3:LITERAL", "LITERAL:PRG");
    expect_iec_file(dr, 0, "3:LITERAL.PRG", "LITERAL.PRG:PRG");
    expect_iec_file(dr, 2, "3:LITERAL.PRG,P,R", "LITERAL.PRG:PRG");
    expect_iec_file(dr, 2, "3:LITERAL.PRG,S,R", "LITERAL.PRG:SEQ");

    expect_iec_file_missing(dr, 0, "3:ONLYBASE.PRG");

    expect_iec_file(dr, 0,
        "3:Q.\xC6\xC3\xC4\xC3\xC6\xC3\xC4\xC3\xC6\xC3\xC4\xC3\xC6\xC3",
        "NASTY:PRG");

    expect_iec_file(dr, 2, "3:BASIC", "BASIC:PRG");
    expect_iec_file(dr, 2, "3:LITERAL.PRG", "LITERAL.PRG:PRG");
    expect_iec_file_missing(dr, 2, "3:BASIC,U");
    return 0;
}

int main(int argc, const char **argv)
{
    UserInterface *ui = new UserInterface("Test Drive");

    IecDrive *dr = new IecDrive();
    t_channel_retval ret;
    uint32_t tr;
    int error = 0;
    mstring status;

    init_ram_disk();
    File *f;
    FileManager *fm = FileManager :: getFileManager();

    create_test_files(fm);
    //error += execute_suite1(fm, dr);
    //error += execute_suite2(fm, dr);
    error += execute_suite3(fm, dr);

    printf("Errors: %d\n", error);

    delete dr;
    delete ui;
    //delete fm;
    return 0;
}
