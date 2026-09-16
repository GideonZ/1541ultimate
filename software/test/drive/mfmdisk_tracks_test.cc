#include "mfmdisk.h"

#include <assert.h>

static void expect_formatted_tracks(int tracks)
{
    MfmDisk disk;
    disk.init(fmt_D81, tracks);

    for (int track = 0; track < WD_MAX_TRACKS_PER_SIDE; ++track) {
        const int expected = track < tracks ? 10 : 0;
        assert(disk.GetTrack(track, 0)->numSectors == expected);
        assert(disk.GetTrack(track, 1)->numSectors == expected);
    }
}

static void expect_track_81_layout()
{
    MfmDisk disk;
    disk.init(fmt_D81, 81);

    MfmSector sector = { 80, 1, 10, 2, 0 };
    uint32_t position = 0;
    uint32_t size = 0;
    assert(disk.GetSector(80, 1, sector, position, size) == 9);
    assert(position == 829440 - 512);
    assert(size == 512);
}

int main()
{
    expect_formatted_tracks(-1);
    expect_formatted_tracks(80);
    expect_formatted_tracks(81);
    expect_track_81_layout();
    expect_formatted_tracks(WD_MAX_TRACKS_PER_SIDE);

    // An oversized image must not write past MfmDisk's fixed track arrays.
    expect_formatted_tracks(WD_MAX_TRACKS_PER_SIDE + 1);
    return 0;
}
