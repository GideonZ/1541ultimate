#ifndef DISK_IMAGE_H_
#define DISK_IMAGE_H_

#include "integer.h"
#include "file_system.h"
#include "blockdev_ram.h"
#include "filetype_d64.h"

#define GCR_DECODER_GCR_IN   (*(volatile BYTE *)0x4060500)
#define GCR_DECODER_BIN_OUT0 (*(volatile BYTE *)0x4060500)
#define GCR_DECODER_BIN_OUT1 (*(volatile BYTE *)0x4060501)
#define GCR_DECODER_BIN_OUT2 (*(volatile BYTE *)0x4060502)
#define GCR_DECODER_BIN_OUT3 (*(volatile BYTE *)0x4060503)
#define GCR_DECODER_ERRORS   (*(volatile BYTE *)0x4060504)

#define GCR_ENCODER_BIN_IN   (*(volatile BYTE *)0x4060508)
#define GCR_ENCODER_GCR_OUT0 (*(volatile BYTE *)0x4060508)
#define GCR_ENCODER_GCR_OUT1 (*(volatile BYTE *)0x4060509)
#define GCR_ENCODER_GCR_OUT2 (*(volatile BYTE *)0x406050A)
#define GCR_ENCODER_GCR_OUT3 (*(volatile BYTE *)0x406050B)
#define GCR_ENCODER_GCR_OUT4 (*(volatile BYTE *)0x406050C)

#define GCR_ENCODER_BIN_IN_32  (*(volatile DWORD *)0x5060508)
#define GCR_DECODER_GCR_IN_32  (*(volatile DWORD *)0x4060500)
#define GCR_DECODER_BIN_OUT_32 (*(volatile DWORD *)0x5060500)


#define C1541_MAXTRACKS   84
#define C1541_MAXTRACKLEN 0x1EF8
#define C1541_MAX_D64_LEN 0x30000
#define C1541_MAX_GCR_LEN (C1541_MAXTRACKLEN * C1541_MAXTRACKS)

class BinImage;

class GcrImage
{
    // disk information
    BYTE id1, id2;
    BYTE _align[2];

    // temporaries that don't need to be allocated over and over
    BYTE header[8];
    BYTE sector_buffer[352]; // 260 for bin sector, 349 for gcr sector + header (352 to be a multiple of 4)
    BYTE gcr_data[C1541_MAX_GCR_LEN];

    // private functions
    BYTE *wrap(BYTE **, BYTE *, BYTE *, int);
    BYTE *find_sync(BYTE *, BYTE *, BYTE *);
    BYTE *convert_block_bin2gcr(BYTE *bin, BYTE *gcr, int len);
    BYTE *convert_track_bin2gcr(int track, BYTE *bin, BYTE *gcr);
    int   find_track_start(int);
public:
    GcrImage();
    ~GcrImage();

    BYTE *dummy_track;

    BYTE  *track_address[C1541_MAXTRACKS];
    BYTE  *gcr_image; // pointer to array, in order to do something special later.
    int    track_length[C1541_MAXTRACKS];

    void blank(void);
    bool load(File *f);
    bool save(File *f, bool, bool);
    bool write_track(int, File *f, bool);
    void convert_disk_bin2gcr(BinImage *bin_image, bool report);
    int  convert_disk_gcr2bin(BinImage *bin_image, bool);
    int  convert_track_gcr2bin(int track, BinImage *bin_image);
    void invalidate(void);
    bool test(void);
    
    friend class BinImage;
};


class BinImage : public PathObject
{
    BYTE *track_start[C1541_MAXTRACKS];
    int   track_sectors[C1541_MAXTRACKS];
    BYTE *errors; // NULL means no error bytes
    int   error_size;

    BlockDevice_Ram *blk;
    Partition *prt;
    FileSystemD64 *fs;
public:
    int   num_tracks;
    BYTE *bin_data;

    BinImage(char *);
    ~BinImage();

    int format(char *diskname);
    int load(File *);
    int save(File *, bool);
    int write_track(int track, GcrImage *, File *);

    // int get_absolute_sector(int track, int sector);
    BYTE * get_sector_pointer(int track, int sector);

    friend class GcrImage;

    // functions that override virtual functions of PathObject
    int fetch_children();
	int get_header_lines() { return 1; }
    void get_sensible_name(char *buffer);
};

extern BinImage static_bin_image; // for general use

#endif /* DISK_IMAGE_H_ */
