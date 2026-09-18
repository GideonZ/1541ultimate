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



// A D64 holding GEOS files, for SI-149. A GEOS file is an ordinary directory entry
// whose type bits say SEQ, PRG or USR, with the info block pointer at offset $15 and
// the file structure at offset $17 filled in as well. The image also holds a plain PRG
// so that the two cases can be compared.
static void d64_make_geos(uint8_t *disk, int dir_index, int info_track, int info_sector,
                          uint8_t structure)
{
    uint8_t *entry = d64_sector(disk, 18, 1) + 32 * dir_index;
    entry[0x15] = (uint8_t)info_track;
    entry[0x16] = (uint8_t)info_sector;
    entry[0x17] = structure;   // 0 is sequential, 1 is VLIR
    entry[0x18] = 6;           // GEOS file type
}

void create_iec_geos_fixture(const char *path)
{
    const char *testname = "create_iec_geos_fixture";
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
    memcpy(bam + 144, "GEOS TEST", 9);
    bam[144 + 21] = '2';
    bam[144 + 22] = 'A';
    d64_set_allocated(bam, 18, 0);
    d64_set_allocated(bam, 18, 1);

    uint8_t *dir = d64_sector(disk, 18, 1);
    dir[0] = 0;
    dir[1] = 0xFF;

    static const uint8_t geosprg[] = "GEOSPRG";
    static const uint8_t geosseq[] = "GEOSSEQ";
    static const uint8_t geosusr[] = "GEOSUSR";
    static const uint8_t plainprg[] = "PLAINPRG";

    // A load address of $0801 in front, so that the PRG case is one a C64 could run.
    d64_add_file(disk, 0, geosprg,  sizeof(geosprg) - 1,  2, 17, 0, "\x01\x08GEOS:PRG");
    d64_add_file(disk, 1, geosseq,  sizeof(geosseq) - 1,  1, 17, 1, "GEOS:SEQ");
    d64_add_file(disk, 2, geosusr,  sizeof(geosusr) - 1,  3, 17, 2, "GEOS:VLIR");
    d64_add_file(disk, 3, plainprg, sizeof(plainprg) - 1, 2, 17, 3, "\x01\x08PLAIN:PRG");

    // One info block, shared by the three GEOS entries. Its first byte is the $03 an
    // info block carries; the rest only has to be recognisable in a CVT stream.
    uint8_t *info = d64_sector(disk, 17, 10);
    info[0] = 0;
    info[1] = 0xFF;
    info[2] = 0x03;
    memcpy(info + 4, "INFO BLOCK", 10);
    d64_set_allocated(bam, 17, 10);

    d64_make_geos(disk, 0, 17, 10, 0);
    d64_make_geos(disk, 1, 17, 10, 0);
    d64_make_geos(disk, 2, 17, 10, 1);

    // The VLIR entry points at a record block, not at a chain of data. A drive hands
    // that block over as it stands, so it is a full sector.
    uint8_t *records = d64_sector(disk, 17, 2);
    memset(records, 0, 256);
    records[0] = 0;
    records[1] = 0xFF;
    records[2] = 17;   // record 0 lives at 17:11
    records[3] = 11;
    memcpy(records + 4, "VLIR", 4);

    FILE *f = fopen(path, "wb");
    REQUIRE(f != NULL);
    REQUIRE(fwrite(disk, 1, D64_SIZE, f) == D64_SIZE);
    REQUIRE(fclose(f) == 0);
    delete[] disk;
}
