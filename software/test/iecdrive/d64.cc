#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "macros.h"

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
    const char *testname = "d64_abs_sector";
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
    const char *testname = "d64_add_file";
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

void create_iec_d64_fixture(const char *path)
{
    const char *testname = "create_iec_d64_fixture";
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


