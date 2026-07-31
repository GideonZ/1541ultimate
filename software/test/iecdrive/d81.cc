#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "macros.h"

static const int D81_TRACKS = 80;
static const int D81_SECTORS_PER_TRACK = 40;
static const int D81_SIZE = 80 * 40 * 256;

static int d81_abs_sector(int track, int sector)
{
    const char *testname = "d81_abs_sector";
    REQUIRE(track >= 1);
    REQUIRE(track <= D81_TRACKS);
    REQUIRE(sector >= 0);
    REQUIRE(sector < 20);

    return (track - 1) * D81_SECTORS_PER_TRACK + sector; 
}

static uint8_t *d81_sector(uint8_t *disk, int track, int sector)
{
    return disk + 256 * d81_abs_sector(track, sector);
}

static void d81_set_allocated(uint8_t *disk, int track, int sector)
{
    uint8_t *bam = (track > 40) ? d81_sector(disk, 40, 2) : d81_sector(disk, 40, 1);
    uint8_t *entry = bam + 16 + 6 * ((track - 1) % 40);
    uint8_t bit = (uint8_t)(1 << (sector & 7));
    if (entry[1 + (sector >> 3)] & bit) {
        entry[1 + (sector >> 3)] &= (uint8_t)~bit;
        entry[0]--;
    }
}

static void d81_add_file(uint8_t *disk, int dir_index, const uint8_t *name, int name_len,
                         uint8_t type, int track, int sector, const char *payload)
{
    const char *testname = "d81_add_file";
    REQUIRE(dir_index >= 0);
    REQUIRE(dir_index < 8);
    REQUIRE(name_len > 0);
    REQUIRE(name_len <= 16);

    uint8_t *dir = d81_sector(disk, 40, 3);
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

    uint8_t *data = d81_sector(disk, track, sector);
    data[0] = 0;
    data[1] = (uint8_t)(payload_len + 1);
    memcpy(data + 2, payload, payload_len);

    d81_set_allocated(disk, track, sector);
}

void create_iec_d81_fixture(const char *path)
{
    const char *testname = "create_iec_d81_fixture";
    uint8_t *disk = new uint8_t[D81_SIZE];
    memset(disk, 0, D81_SIZE);

    uint8_t *root = d81_sector(disk, 40, 0);
    root[0] = 40;
    root[1] = 3;
    root[2] = 0x44;
    root[3] = 0x00;
    memset(root+4, 0xA0, 25);
    memcpy(root+4, "IEC TEST", 8);
    root[0x19] = '3';
    root[0x1A] = 'D';

    uint8_t *bam1 = d81_sector(disk, 40, 1);
    uint8_t *bam2 = d81_sector(disk, 40, 2);
    bam1[0] = 40;
    bam1[1] = 2;
    bam1[2] = 0x44;
    bam1[3] = 0xBB;
    bam1[4] = 0xA0;
    bam1[5] = 0xA0;
    bam1[6] = 0xC0;

    bam2[0] = 0;
    bam2[1] = 0xFF;
    bam2[2] = 0x44;
    bam2[3] = 0xBB;
    bam2[4] = 0xA0;
    bam2[5] = 0xA0;
    bam2[6] = 0xC0;

    memset(bam1 + 16, 0xFF, 40*6);
    memset(bam2 + 16, 0xFF, 40*6);
    for (int track = 0; track < 40; track++) {
        int offset = 16 + 6*track;
        bam1[offset] = 40;
        bam2[offset] = 40;
    }

    d81_set_allocated(disk, 40, 0);
    d81_set_allocated(disk, 40, 1);
    d81_set_allocated(disk, 40, 2);
    d81_set_allocated(disk, 40, 3);

    uint8_t *dir = d81_sector(disk, 40, 3);
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

    d81_add_file(disk, 0, basic, sizeof(basic) - 1, 2, 39, 0, "BASIC:PRG");
    d81_add_file(disk, 1, basic, sizeof(basic) - 1, 1, 39, 1, "BASIC:SEQ");
    d81_add_file(disk, 2, literal, sizeof(literal) - 1, 2, 39, 2, "LITERAL:PRG");
    d81_add_file(disk, 3, literal_prg, sizeof(literal_prg) - 1, 2, 39, 3, "LITERAL.PRG:PRG");
    d81_add_file(disk, 4, literal_prg, sizeof(literal_prg) - 1, 1, 39, 4, "LITERAL.PRG:SEQ");
    d81_add_file(disk, 5, onlybase, sizeof(onlybase) - 1, 2, 39, 5, "ONLYBASE:PRG");
    d81_add_file(disk, 6, nasty, sizeof(nasty), 2, 39, 6, "NASTY:PRG");

    FILE *f = fopen(path, "wb");
    REQUIRE(f != NULL);
    REQUIRE(fwrite(disk, 1, D81_SIZE, f) == D81_SIZE);
    REQUIRE(fclose(f) == 0);
    delete[] disk;
}


