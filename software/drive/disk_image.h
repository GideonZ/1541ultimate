#ifndef DISK_IMAGE_H_
#define DISK_IMAGE_H_

#include "integer.h"
#include "file_system.h"
#include "blockdev_ram.h"
#include "filesystem_d64.h"
#include "iomap.h"
#include "userinterface.h"
#include "menu.h"
#include "filemanager.h"
#include "subsys.h"

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


#define GCRIMAGE_MAXUSEDTRACKS  80
#define GCRIMAGE_MAXHDRTRACKS   168
#define GCRIMAGE_FIRSTTRACKSIDE1 84

#define GCRIMAGE_MAXMETADATA    (12 + (GCRIMAGE_MAXHDRTRACKS * 10)) // 10 bytes: 2 len, 4 pointer, 4 speed zone
#define GCRIMAGE_MAXTRACKLEN    0x1EF8
#define GCRIMAGE_DUMMYTRACKLEN  0x1E0C

#define GCRIMAGE_MAXSIZE        ((GCRIMAGE_MAXTRACKLEN * GCRIMAGE_MAXUSEDTRACKS) + GCRIMAGE_MAXMETADATA)

#define C1541_MAX_D64_LEN            (201216)
#define C1541_MAX_D64_35_NO_ERRORS   (174848)
#define C1541_MAX_D64_35_WITH_ERRORS (175531)
#define C1541_MAX_D64_40_NO_ERRORS   (196608)
#define C1541_MAX_D64_40_WITH_ERRORS (197376)
#define C1541_MIN_D71_SIZE           (2*C1541_MAX_D64_35_NO_ERRORS)
#define C1541_D71_SIZE_WITH_ERRORS   (C1541_MIN_D71_SIZE + 1366)

#define MFM_TRACK_HEADER_SIZE        2 + (WD_MAX_SECTORS_PER_TRACK * 5)

class BinImage;

struct GcrTrack
{
    uint8_t  *track_address;
    int       track_length; // double sided
    int       speed_zone;
    bool      track_used;
    bool      in_image_file;
    bool      track_is_mfm;
};

class GcrImage
{
    // disk information
    uint8_t id1, id2;
    uint8_t _align[2];

    // temporaries that don't need to be allocated over and over
    uint8_t header[8];
    uint8_t sector_buffer[352]; // 260 for bin sector, 349 for gcr sector + header (352 to be a multiple of 4)

    uint8_t *gcr_data;
    GcrTrack tracks[GCRIMAGE_MAXHDRTRACKS];
    bool  double_sided;

    // private functions
    static uint8_t *wrap(uint8_t **, uint8_t *, uint8_t *, int, uint8_t *buffer, uint8_t shift);
    static uint8_t *find_sync(uint8_t *, uint8_t *, uint8_t *);
    uint8_t *convert_block_bin2gcr(uint8_t *bin, uint8_t *gcr, int len);
    uint8_t *convert_track_bin2gcr(uint8_t logical_track_1b, int region, uint8_t *bin, uint8_t *gcr, uint8_t *errors, int errors_size, int geosgaps);
    int   find_track_start(int);
    void add_blank_tracks(uint8_t *);
public:
    GcrImage();
    ~GcrImage();

    void dump(void);
    void blank(void);
    bool load(File *f);
    bool save(File *f, bool, UserInterface *ui);
    bool write_track(int, File *f, bool);
    void convert_disk_bin2gcr(BinImage *bin_image, UserInterface *ui, int geoscopyprot);
    int  convert_disk_gcr2bin(BinImage *bin_image, UserInterface *ui);
    int  convert_track_gcr2bin(int track, BinImage *bin_image, int &errors);
    void invalidate(void);
    bool test(void);
    void set_ds(bool);
    bool is_double_sided(void);
    void track_got_written_with_gcr(int track);
    
    friend class BinImage;
    friend class C1541;
    static int convert_gcr_track_to_bin(uint8_t *gcr, int trackNumber, int trackLength,
    		int maxSector, uint8_t *bin, uint8_t *status, int statusLength);
};

#define BINIMAGE_MAXTRACKS 80

class BinImage
{
    uint8_t *track_start[BINIMAGE_MAXTRACKS];
    int      track_sectors[BINIMAGE_MAXTRACKS];
    uint8_t *errors; // NULL means no error bytes
    int   error_size;
    int   allocated_size;
    int   data_size;
    bool  double_sided;

    int init(uint32_t size);
public:
    int   num_tracks;
    uint8_t *bin_data;

    BinImage(const char *, int tracks);
    ~BinImage();

    int format(const char *diskname);
    int copy(uint8_t *, uint32_t size);
    int load(File *);
    int save(File *, UserInterface *ui);
    int write_track(int track, GcrImage *, File *, int& numerrors, int& secs);
    bool is_double_sided(void);
    uint8_t * get_sector_pointer(int track, int sector);

    friend class GcrImage;

    void get_sensible_name(char *buffer);
};

extern BinImage static_bin_image; // for general use

class ImageCreator : public ObjectWithMenu
{
    TaskCategory *taskCategory;
    Action *d64, *g64, *d71, *g71, *d81, *d81_81, *dnp;
public:
	ImageCreator() {
	    taskCategory = TasksCollection :: getCategory("Create", SORT_ORDER_CREATE);
	    d64 = g64 = d71 = g71 = d81 = d81_81 = dnp = NULL;
	}
	~ImageCreator() { }

	static SubsysResultCode_e S_createD64(SubsysCommand *cmd);
    static SubsysResultCode_e S_createD71(SubsysCommand *cmd);
    static SubsysResultCode_e S_createD81(SubsysCommand *cmd);
    static SubsysResultCode_e S_createD81_81(SubsysCommand *cmd);
    static SubsysResultCode_e S_createDNP(SubsysCommand *cmd);

	// object with menu
    void create_task_items(void)
    {
        d64 = new Action("D64 Image", ImageCreator :: S_createD64, 0, 0);
        g64 = new Action("G64 Image", ImageCreator :: S_createD64, 0, 1);
        d71 = new Action("D71 Image", ImageCreator :: S_createD71, 0, 0); // may also be created using createD64 with mode 2
        g71 = new Action("G71 Image", ImageCreator :: S_createD64, 0, 3);
        d81 = new Action("D81 Image", ImageCreator :: S_createD81, 0, 0);
        d81_81 = new Action("D81 (81 Tr.)", ImageCreator :: S_createD81_81, 0, 0);
        dnp = new Action("DNP Image", ImageCreator :: S_createDNP, 0, 0);
        taskCategory->append(d64);
        taskCategory->append(g64);
        taskCategory->append(d71);
        taskCategory->append(g71);
        taskCategory->append(d81);
        taskCategory->append(d81_81);
        taskCategory->append(dnp);
    }

	void update_task_items(bool writablePath)
	{
	    d64->setDisabled(!writablePath);
        g64->setDisabled(!writablePath);
        d71->setDisabled(!writablePath);
        g71->setDisabled(!writablePath);
        d81->setDisabled(!writablePath);
        d81_81->setDisabled(!writablePath);
        dnp->setDisabled(!writablePath);
	}
};

#endif /* DISK_IMAGE_H_ */
