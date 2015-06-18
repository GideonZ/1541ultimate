#ifndef DISK_IMAGE_H_
#define DISK_IMAGE_H_

#include "integer.h"
#include "file_system.h"
#include "blockdev_ram.h"
#include "d64_filesystem.h"
#include "iomap.h"
#include "userinterface.h"

#define GCR_DECODER_GCR_IN   (*(volatile uint8_t *)(GCR_CODER_BASE + 0x00))
#define GCR_DECODER_BIN_OUT0 (*(volatile uint8_t *)(GCR_CODER_BASE + 0x00))
#define GCR_DECODER_BIN_OUT1 (*(volatile uint8_t *)(GCR_CODER_BASE + 0x01))
#define GCR_DECODER_BIN_OUT2 (*(volatile uint8_t *)(GCR_CODER_BASE + 0x02))
#define GCR_DECODER_BIN_OUT3 (*(volatile uint8_t *)(GCR_CODER_BASE + 0x03))
#define GCR_DECODER_ERRORS   (*(volatile uint8_t *)(GCR_CODER_BASE + 0x04))

#define GCR_ENCODER_BIN_IN   (*(volatile uint8_t *)(GCR_CODER_BASE + 0x08))
#define GCR_ENCODER_GCR_OUT0 (*(volatile uint8_t *)(GCR_CODER_BASE + 0x08))
#define GCR_ENCODER_GCR_OUT1 (*(volatile uint8_t *)(GCR_CODER_BASE + 0x09))
#define GCR_ENCODER_GCR_OUT2 (*(volatile uint8_t *)(GCR_CODER_BASE + 0x0A))
#define GCR_ENCODER_GCR_OUT3 (*(volatile uint8_t *)(GCR_CODER_BASE + 0x0B))
#define GCR_ENCODER_GCR_OUT4 (*(volatile uint8_t *)(GCR_CODER_BASE + 0x0C))

#define GCR_ENCODER_BIN_IN_32  (*(volatile uint32_t *)(GCR_CODER_BASE + 0x08))
#define GCR_DECODER_GCR_IN_32  (*(volatile uint32_t *)(GCR_CODER_BASE + 0x00))
#define GCR_DECODER_BIN_OUT_32 (*(volatile uint32_t *)(GCR_CODER_BASE + 0x00))


#define C1541_MAXTRACKS   84
#define C1541_MAXTRACKLEN 0x1EF8
#define C1541_MAX_D64_LEN 201216
#define C1541_MAX_GCR_LEN (C1541_MAXTRACKLEN * C1541_MAXTRACKS)
#define C1541_MAX_D64_35_NO_ERRORS (174848)
#define C1541_MAX_D64_35_WITH_ERRORS (175531)
#define C1541_MAX_D64_40_NO_ERRORS (196608)
#define C1541_MAX_D64_40_WITH_ERRORS (197376)

class BinImage;

class GcrImage
{
    // disk information
    uint8_t id1, id2;
    uint8_t _align[2];

    // temporaries that don't need to be allocated over and over
    uint8_t header[8];
    uint8_t sector_buffer[352]; // 260 for bin sector, 349 for gcr sector + header (352 to be a multiple of 4)
    uint8_t gcr_data[C1541_MAX_GCR_LEN];

    // private functions
    uint8_t *wrap(uint8_t **, uint8_t *, uint8_t *, int);
    uint8_t *find_sync(uint8_t *, uint8_t *, uint8_t *);
    uint8_t *convert_block_bin2gcr(uint8_t *bin, uint8_t *gcr, int len);
    uint8_t *convert_track_bin2gcr(int track, uint8_t *bin, uint8_t *gcr, uint8_t *errors, int errors_size);
    int   find_track_start(int);
public:
    GcrImage();
    ~GcrImage();

    uint8_t *dummy_track;

    uint8_t  *track_address[C1541_MAXTRACKS];
    uint8_t  *gcr_image; // pointer to array, in order to do something special later.
    int    track_length[C1541_MAXTRACKS];

    void blank(void);
    bool load(File *f);
    bool save(File *f, bool, UserInterface *ui);
    bool write_track(int, File *f, bool);
    void convert_disk_bin2gcr(BinImage *bin_image, UserInterface *ui);
    int  convert_disk_gcr2bin(BinImage *bin_image, UserInterface *ui);
    int  convert_track_gcr2bin(int track, BinImage *bin_image);
    void invalidate(void);
    bool test(void);
    
    friend class BinImage;
};


class BinImage
{
    uint8_t *track_start[C1541_MAXTRACKS];
    int   track_sectors[C1541_MAXTRACKS];
    uint8_t *errors; // NULL means no error bytes
    int   error_size;

    BlockDevice_Ram *blk;
    Partition *prt;
    FileSystemD64 *fs;
public:
    int   num_tracks;
    uint8_t *bin_data;

    BinImage(const char *);
    ~BinImage();

    int format(const char *diskname);
    int load(File *);
    int save(File *, UserInterface *ui);
    int write_track(int track, GcrImage *, File *);

    // int get_absolute_sector(int track, int sector);
    uint8_t * get_sector_pointer(int track, int sector);

    friend class GcrImage;

    void get_sensible_name(char *buffer);
};

extern BinImage static_bin_image; // for general use

#endif /* DISK_IMAGE_H_ */
