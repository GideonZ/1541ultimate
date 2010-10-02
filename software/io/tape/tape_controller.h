/*************************************************************/
/* Tape Emulator / Recorder Control                          */
/*************************************************************/
#include "file_system.h"
#include "event.h"
#include "poll.h"

#define PLAYBACK_STATUS  *((volatile BYTE *)0x40A0000)
#define PLAYBACK_CONTROL *((volatile BYTE *)0x40A0000)
#define PLAYBACK_DATA     ((volatile BYTE *)0x40A0800)

#define C2N_ENABLE      0x01
#define C2N_CLEAR_ERROR 0x02
#define C2N_FLUSH_FIFO  0x04
#define C2N_MODE_SELECT 0x08

#define C2N_STAT_ENABLED    0x01
#define C2N_STAT_ERROR      0x02
#define C2N_STAT_FIFO_FULL  0x04
#define C2N_STAT_FIFO_AF    0x08
#define C2N_STAT_STREAM_EN  0x40
#define C2N_STAT_FIFO_EMPTY 0x80

class TapeController
{
	File *file;
	DWORD length;
	int   block;
    int   mode;

	void read_block();
public:
	TapeController();
	~TapeController();
	
	void stop();
	void start();
	void poll(Event &);
	void set_file(File *, DWORD, int);
};

extern TapeController *tape_controller;
