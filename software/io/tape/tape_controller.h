/*************************************************************/
/* Tape Emulator / Recorder Control                          */
/*************************************************************/
#include <stdio.h>
#include "menu.h"
#include "iomap.h"
#include "filemanager.h"
#include "subsys.h"
#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define PLAYBACK_STATUS  *((volatile uint8_t *)(C2N_PLAY_BASE + 0x000))
#define PLAYBACK_CONTROL *((volatile uint8_t *)(C2N_PLAY_BASE + 0x000))
#define PLAYBACK_DATA     ((volatile uint8_t *)(C2N_PLAY_BASE + 0x800))

#define C2N_ENABLE      0x01
#define C2N_CLEAR_ERROR 0x02
#define C2N_FLUSH_FIFO  0x04
#define C2N_MODE_SELECT 0x08
#define C2N_SENSE		0x10
#define C2N_RATE        0x20

#define C2N_OUT_READ    0x00
#define C2N_OUT_WRITE   0x40
#define C2N_OUT_WRITE_N 0x80
#define C2N_OUT_TOGGLE  0xC0

#define C2N_STAT_ENABLED    0x01
#define C2N_STAT_ERROR      0x02
#define C2N_STAT_FIFO_FULL  0x04
#define C2N_STAT_FIFO_AF    0x08
#define C2N_STAT_STREAM_EN  0x40
#define C2N_STAT_FIFO_EMPTY 0x80

class TapeController : public SubSystem, ObjectWithMenu, ConfigurableObject
{
	FileManager *fm;
	File *file;
	uint32_t length;
	int   state;
	int   block;
    int   mode;
	int   paused;
	bool  recording;
	uint8_t  controlByte;
	uint8_t  *blockBuffer;
	TaskHandle_t taskHandle;

	struct {
	    Action *pause;
	    Action *resume;
	    Action *stop;
	    Action *rewind;
	} myActions;

	void read_block();
	static void poll_static(void *a);
public:
	TapeController();
	virtual ~TapeController();
	
    void create_task_items(void);
    void update_task_items(bool writablePath);
    const char *identify(void) { return "Tape Player"; }
	SubsysResultCode_e executeCommand(SubsysCommand *cmd);
	
	void close();
	void stop();
	void start(int);
	void poll();
	void set_file(File *f, uint32_t, int, int);
};

extern TapeController *tape_controller;
