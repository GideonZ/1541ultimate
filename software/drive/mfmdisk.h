#ifndef MFMDISK_H
#define MFMDISK_H

#include <stdint.h>

#define WD_MAX_SECTORS_PER_TRACK    32
#define WD_MAX_TRACKS_PER_SIDE      84

typedef enum {
    fmt_Clear,
    fmt_D81,
} MfmFormat_t;

struct MfmSector
{
    uint8_t track; // logical track number as stored on the disk
    uint8_t side;  // logical side number, as stored on the disk
    uint8_t sector; // logical sector number, as stored on the disk
    uint8_t sector_size; // 00 = 128, 01 = 256, 02 = 512, 03 = 1024.
    uint8_t error_byte;  // requested by MarkusC64
};

struct MfmTrack
{
    int numSectors;          // if 0, the track is not defined in the file
    uint32_t offsetInFile;   // Where does this track start
    uint32_t reservedSpace;  // How much space is allocated for the track data
    uint32_t actualDataSize; // How much space the track actually occupies
    MfmSector sectors[WD_MAX_SECTORS_PER_TRACK];
};

class MfmDisk
{
    MfmTrack side0[WD_MAX_TRACKS_PER_SIDE];
    MfmTrack side1[WD_MAX_TRACKS_PER_SIDE];
public:
    MfmDisk();
    int Load(const char *filename);
    int Save(const char *filename);

    // Returns the file / buffer offset of the requested sector in pos
    // Returns index of found sector, < 0 for error
    int GetSector(int physTrack, int physSide, const MfmSector& logicalSector, uint32_t& pos, uint32_t& sectSize);

    // Get Address returns the sector header information of the next upcoming sector, given by index
    int GetAddress(int physTrack, int physSide, int lastIndex, MfmSector& addr);

    // Returns error code if new data does not fit. Returns position to write data to in 'pos', if all OK.
    int UpdateTrack(int physTrack, int physSide, MfmTrack& newTrack, uint32_t& pos);

    // Defines new space for the track. Sets the used size to 0. Use prior to UpdateTrack
    int AddDataSpace(int physTrack, int physSide, const uint32_t pos, const uint32_t size);

    // Predefined Formats. Returns the required data size
    uint32_t init(MfmFormat_t type, int tracks);

    // Get track reference for external manipulation
    MfmTrack *GetTrack(int track, int side);

    // Just for debug
    void DumpFormat(bool);

};


#endif
