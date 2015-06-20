#ifndef COPPER_H
#define COPPER_H

#include "c64.h"
#include "event.h"
#include "poll.h"
#include "menu.h"
#include "iomap.h"
#include "path.h"

#define COPPER_COMMAND     *((volatile BYTE *)(COPPER_BASE + 0x00))
#define COPPER_STATUS      *((volatile BYTE *)(COPPER_BASE + 0x01))
#define COPPER_MEASURE_L   *((volatile BYTE *)(COPPER_BASE + 0x02))
#define COPPER_MEASURE_H   *((volatile BYTE *)(COPPER_BASE + 0x03))
#define COPPER_FRAMELEN_L  *((volatile BYTE *)(COPPER_BASE + 0x04))
#define COPPER_FRAMELEN_H  *((volatile BYTE *)(COPPER_BASE + 0x05))
#define COPPER_BREAK       *((volatile BYTE *)(COPPER_BASE + 0x06))
#define COPPER_RAM(x)      *((volatile BYTE *)(COPPER_BASE + 0x1000 + x))
#define COPPER_RAM_SIZE    4096

#define COPPER_CMD_PLAY    0x01
#define COPPER_CMD_RECORD  0x02
 
#define COPCODE_WRITE_REG   0x00
#define COPCODE_READ_REG    0x40
#define COPCODE_WAIT_IRQ    0x81
#define COPCODE_WAIT_SYNC   0x82
#define COPCODE_TIMER_CLR   0x83
#define COPCODE_CAPTURE     0x84
#define COPCODE_WAIT_FOR    0x85
#define COPCODE_WAIT_UNTIL  0x86
#define COPCODE_REPEAT      0x87
#define COPCODE_END         0x88
#define COPCODE_TRIGGER_1   0x89
#define COPCODE_TRIGGER_2   0x8A

class Copper : public ObjectWithMenu
{
    bool synchronized;

    void measure(void);
    void sync(void);
    void timed_write(void);
    void capture(void);
    void write_state(void);
    void sweep(void);
    
public:
	Copper();
	~Copper();

	int  fetch_task_items(Path *path, IndexedList<CachedTreeNode*> &item_list);
	void poll(Event &);
	
};

#endif
