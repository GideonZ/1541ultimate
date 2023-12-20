#include "mfmdisk.h"
#include <string.h>
#include <stdio.h>

MfmDisk :: MfmDisk()
{
    memset(side0, 0, sizeof(side0));
    memset(side1, 0, sizeof(side1));
}

int MfmDisk :: GetSector(int physTrack, int physSide, const MfmSector& logicalSector, uint32_t& pos, uint32_t& sectSize )
{
    if ((physTrack < 0) || (physTrack >= WD_MAX_TRACKS_PER_SIDE))
        return -1;
    if ((physSide < 0) || (physSide > 1))
        return -1;
    MfmTrack *tr = (physSide) ? &side1[physTrack] : &side0[physTrack];
    uint32_t currentPos = tr->offsetInFile;
    for(int i=0; i<tr->numSectors; i++) {
        MfmSector *s = &tr->sectors[i];
        if ((s->sector == logicalSector.sector) &&
            //(s->side == logicalSector.side) &&  // Checking the side here is possibly not done by the Wd177x, as there is no 'side' register
            (s->track == logicalSector.track)) {
            pos = currentPos;
            sectSize = (s->sector_size < 4) ? (1 << 7 + s->sector_size) : 512;
            return i;
        }
        currentPos += (1 << (7 + s->sector_size));
    }
    return -2;
}

int MfmDisk :: GetAddress(int physTrack, int physSide, int lastIndex, MfmSector& addr)
{
    if ((physTrack < 0) || (physTrack >= WD_MAX_TRACKS_PER_SIDE))
        return -1;
    if ((physSide < 0) || (physSide > 1))
        return -1;
    MfmTrack *tr = (physSide) ? &side1[physTrack] : &side0[physTrack];
    if (!tr->numSectors) {
        return -1;
    }
    int idx = lastIndex + 1;
    if (idx >= tr->numSectors) {
        idx = 0;
    }
    // copy information
    addr = tr->sectors[idx];
    // return new index
    return idx;
}

int MfmDisk :: UpdateTrack(int physTrack, int physSide, MfmTrack& newTrack, uint32_t& pos)
{
    if ((physTrack < 0) || (physTrack >= WD_MAX_TRACKS_PER_SIDE))
        return -1;
    if ((physSide < 0) || (physSide > 1))
        return -1;
    MfmTrack *tr = (physSide) ? &side1[physTrack] : &side0[physTrack];

    // calculate required space for new track
    uint32_t requiredSize = 0;
    for (int i=0; i<newTrack.numSectors; i++) {
        MfmSector *s = &newTrack.sectors[i];
        requiredSize += (1 << 7 + s->sector_size);
    }
    if (requiredSize > tr->reservedSpace) {
        return -2;
    }
    pos = tr->offsetInFile;
    tr->actualDataSize = requiredSize;
    tr->numSectors = newTrack.numSectors;
    for (int i=0; i<newTrack.numSectors; i++) {
        tr->sectors[i] = newTrack.sectors[i];
    }
    return 0; // OK!
}

int MfmDisk :: AddDataSpace(int physTrack, int physSide, const uint32_t pos, const uint32_t size)
{
    if ((physTrack < 0) || (physTrack >= WD_MAX_TRACKS_PER_SIDE))
        return -1;
    if ((physSide < 0) || (physSide > 1))
        return -1;
    MfmTrack *tr = (physSide) ? &side1[physTrack] : &side0[physTrack];
    tr->offsetInFile = pos;
    tr->reservedSpace = size;
    tr->numSectors = 0;
    tr->actualDataSize = 0;
    return 0;
}

uint32_t MfmDisk :: init(MfmFormat_t type, int tracks)
{
    uint32_t size = 0;
    uint32_t offset = 0;

    printf("MfmDiskInit: Sizeof side0 = %d\n", sizeof(side0));
    memset(side0, 0, sizeof(side0));
    memset(side1, 0, sizeof(side1));

    switch(type) {
    case fmt_D81:
        for (int t=0;t<tracks;t++) {
            side0[t].actualDataSize = 5120;
            side0[t].reservedSpace = 5120;
            side0[t].numSectors = 10;
            side0[t].offsetInFile = offset;
            offset += 5120;

            side1[t].actualDataSize = 5120;
            side1[t].reservedSpace = 5120;
            side1[t].numSectors = 10;
            side1[t].offsetInFile = offset;
            offset += 5120;

            for (int s=0; s<10; s++) {
                side0[t].sectors[s].sector = (uint8_t)(s+1);
                side0[t].sectors[s].sector_size = 2;
                side0[t].sectors[s].side = 0;
                side0[t].sectors[s].track = (uint8_t)t;

                side1[t].sectors[s].sector = (uint8_t)(s+1);
                side1[t].sectors[s].sector_size = 2;
                side1[t].sectors[s].side = 1;
                side1[t].sectors[s].track = (uint8_t)t;
            }
        }
        break;
    }
    return size;
}

MfmTrack *MfmDisk :: GetTrack(int physTrack, int physSide)
{
    if ((physTrack < 0) || (physTrack >= WD_MAX_TRACKS_PER_SIDE))
        return NULL;
    if ((physSide < 0) || (physSide > 1))
        return NULL;
    return (physSide) ? &side1[physTrack] : &side0[physTrack];
}

void MfmDisk :: DumpFormat(bool showSectors)
{
    printf("Trk#  Secs  Data  Offset | ");
    printf("Secs  Data  Offset \n");

    char secs[60];
    secs[0] = 0;

    for(int i=0;i < WD_MAX_TRACKS_PER_SIDE;i++) {
        MfmTrack *t;
        t = &side0[i];
        if (showSectors) {
            sprintf(secs, "(0:%d/%2d/%d, 1:%d/%2d/%d, 2:%d/%2d/%d, ...)", t->sectors[0].side, t->sectors[0].track, t->sectors[0].sector,
                t->sectors[1].side, t->sectors[1].track, t->sectors[1].sector,
                t->sectors[2].side, t->sectors[2].track, t->sectors[2].sector);
        }

        printf("%4d  %4d  %4d  %6x %s", i, t->numSectors, t->actualDataSize, t->offsetInFile, secs);

        t = &side1[i];

        if (showSectors) {
            sprintf(secs, "(0:%d/%2d/%d, 1:%d/%2d/%d, 2:%d/%2d/%d, ...)", t->sectors[0].side, t->sectors[0].track, t->sectors[0].sector,
                t->sectors[1].side, t->sectors[1].track, t->sectors[1].sector,
                t->sectors[2].side, t->sectors[2].track, t->sectors[2].sector);
        }
        printf("| %4d  %4d  %6x %s\n", t->numSectors, t->actualDataSize, t->offsetInFile, secs);
    }
}
