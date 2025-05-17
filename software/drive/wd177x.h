#ifndef WD177X_H
#define WD177X_H

#include "mm_drive.h"
#include "mfmdisk.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "filemanager.h"

#define WD_STATUS_BUSY      0x01
#define WD_STATUS_INDEX     0x02
#define WD_STATUS_DRQ       0x02
#define WD_STATUS_LOST      0x04
#define WD_STATUS_TRACK_00  0x04
#define WD_STATUS_CRC_ERROR 0x08
#define WD_STATUS_RNF		0x10
#define WD_STATUS_SPINUP_OK 0x20
#define WD_STATUS_REC_TYPE  0x20
#define WD_STATUS_WRITEPROT 0x40
#define WD_STATUS_MOTOR_ON  0x80

#define WD_CMD_RESTORE      0
#define WD_CMD_SEEK    	    1
#define WD_CMD_STEP    	    2
#define WD_CMD_STEP_IN 		4
#define WD_CMD_STEP_OUT     6
#define WD_CMD_READ_SECTOR  8
#define WD_CMD_WRITE_SECTOR 10
#define WD_CMD_READ_ADDR    12
#define WD_CMD_ABORT        13
#define WD_CMD_READ_TRACK   14
#define WD_CMD_WRITE_TRACK  15

#define WD_CMD_UPDATE_BIT   0x10
#define WD_CMD_SPINUP_BIT   0x08
#define WD_CMD_VERIFY_BIT   0x04

#define WD_CMD_DMA_DONE     0x100 // bit 8

typedef struct _wd {
	uint8_t command;
	uint8_t track;
	uint8_t sector;
	uint8_t datareg;
	uint8_t status_clear;
	uint8_t status_set;
	uint8_t irq_ack;
	uint8_t dma_mode;
	uint32_t dma_addr; // little endian
	uint16_t dma_len; // little endian
	uint8_t stepper_track;
	uint8_t step_time;
} wd177x_t;

typedef uint16_t t_wd177x_cmd;

#define MFM_RAW_BYTES_BETWEEN_INDEX_PULSES  6250
#define MFM_DEFAULT_SECTOR_SIZE             512
#define MFM_DEFAULT_SECTORS_PER_TRACK  	    10
#define MFM_BYTES_PER_TRACK			        (MFM_DEFAULT_SECTOR_SIZE * MFM_DEFAULT_SECTORS_PER_TRACK)

typedef void (*track_update_callback_t)(void *obj, int physTrack, int side, MfmTrack *track);

class WD177x
{
    volatile wd177x_t *wd177x;
    volatile mmdrive_regs_t *drive;
    int irqNr;

    FileManager *fm;
    File *mount_file;
    TaskHandle_t taskHandle;
    QueueHandle_t cmdQueue;
    MfmDisk disk;
    track_update_callback_t track_updater;

    void *track_update_object;

    uint8_t buffer[MFM_RAW_BYTES_BETWEEN_INDEX_PULSES+2];
    uint8_t binbuf[MFM_BYTES_PER_TRACK];
    uint8_t step_dir;//, track, side
    int      sectIndex;
    uint32_t sectSize;
    uint32_t offset; // need to remember it because write sector is implemented with two calls to the handler

    static void run(void *a);

    void task();
    int  decode_write_track(uint8_t *inbuf, uint8_t *outbuf, MfmTrack& newTrack);
    void do_step(t_wd177x_cmd cmd);
    void wait_head_settle(void);
    void handle_wd177x_completion(t_wd177x_cmd& cmd); // called from within
public:
    WD177x(volatile uint8_t *wd, volatile uint8_t *drv, int irq);
    ~WD177x();

    void init(void);
    uint8_t IrqHandler(void);
    void format_d81(int);
    void set_file(File *);
    MfmDisk *get_disk(void);
    void set_track_update_callback(void *obj, track_update_callback_t func);

    BaseType_t check_queue(t_wd177x_cmd& cmd); // called from drive class context
    void handle_wd177x_command(t_wd177x_cmd& cmd); // called from drive class context
};

#endif

