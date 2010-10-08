/*************************************************************/
/* Tape Recorder Control                                     */
/*************************************************************/
#include "file_system.h"
#include "event.h"
#include "poll.h"

#define RECORD_STATUS  *((volatile BYTE *)0x40C0000)
#define RECORD_CONTROL *((volatile BYTE *)0x40C0000)
#define RECORD_DATA     ((volatile BYTE *)0x40C0800)

#define REC_ENABLE       0x01
#define REC_CLEAR_ERROR  0x02
#define REC_FLUSH_FIFO   0x04
#define REC_MODE_RISING  0x00
#define REC_MODE_FALLING 0x10
#define REC_MODE_BOTH    0x20

#define REC_STAT_ENABLED    0x01
#define REC_STAT_ERROR      0x02
#define REC_STAT_FIFO_FULL  0x04
#define REC_STAT_BLOCK_AV   0x08
#define REC_STAT_STREAM_EN  0x40
#define REC_STAT_BYTE_AV    0x80

class TapeRecorder
{
	File *file;
	DWORD length;
	int   block;
    int   mode;

	void write_block();
public:
	TapeRecorder();
	~TapeRecorder();
	
	void stop();
	void start();
	void poll(Event &);
	void set_file(File *, DWORD, int);
};

extern TapeController *tape_controller;
