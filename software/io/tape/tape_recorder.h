/*************************************************************/
/* Tape Recorder Control                                     */
/*************************************************************/
#include <stdio.h>
#include "menu.h"
#include "iomap.h"
#include "filemanager.h"
#include "subsys.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define RECORD_STATUS  *((volatile uint8_t *)(C2N_RECORD_BASE + 0x000))
#define RECORD_CONTROL *((volatile uint8_t *)(C2N_RECORD_BASE + 0x000))
#define RECORD_DATA     ((volatile uint8_t *)(C2N_RECORD_BASE + 0x800))
#define RECORD_DATA32  *((volatile uint32_t*)(C2N_RECORD_BASE + 0x800))

#define REC_ENABLE       0x01
#define REC_CLEAR_ERROR  0x02
#define REC_FLUSH_FIFO   0x04
#define REC_MODE_RISING  0x00
#define REC_MODE_FALLING 0x10
#define REC_MODE_BOTH    0x20
#define REC_SELECT_READ  0x00
#define REC_SELECT_WRITE 0x40
#define REC_IRQ_EN       0x80

#define REC_STAT_ENABLED    0x01
#define REC_STAT_ERROR      0x02
#define REC_STAT_FIFO_FULL  0x04
#define REC_STAT_BLOCK_AV   0x08
#define REC_STAT_STREAM_EN  0x40
#define REC_STAT_BYTE_AV    0x80

#define REC_NUM_CACHE_BLOCKS 128
#define REC_CACHE_SIZE (REC_NUM_CACHE_BLOCKS * 512)

#define REC_ERR_OK          0
#define REC_ERR_OVERFLOW    1
#define REC_ERR_NO_FILE     2
#define REC_ERR_WRITE_ERROR 3

class TapeRecorder : public ObjectWithMenu
{
	FileManager *fm;
	TaskHandle_t taskHandle;
    UserInterface *last_user_interface;
	File *file;
    int   error_code;
	int   recording;
    int   select;
    int   block_in;
    int   block_out;
    int   blocks_cached;
    int   blocks_written;
    int   total_length;
	int   write_block();
    void  cache_block();
    uint8_t *cache;
    uint32_t *cache_blocks[REC_NUM_CACHE_BLOCKS];
    static void poll_tape_rec(void *a);
	void poll(void);
public:
	TapeRecorder();
	virtual ~TapeRecorder();

	int  fetch_task_items(Path *path, IndexedList<Action*> &item_list);
	int executeCommand(SubsysCommand *cmd);
    const char *identify(void) { return "Tape Recorder"; }
	
    void flush();
	void stop(int);
	void start();
	bool request_file(SubsysCommand *);
    void irq();
};

extern TapeRecorder *tape_recorder;
