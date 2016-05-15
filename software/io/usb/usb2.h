#ifndef USB2_H
#define USB2_H

#include "usb_base.h"
#include "usb_device.h"
#include "usb_nano.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

class UsbDevice;
class UsbDriver;


extern "C" BaseType_t usb_irq(void);

struct usb_event {
	uint16_t fifo_word[2];
};

struct usb_packet {
	uint8_t *data;
	void *object;
	uint16_t  length;
	uint16_t  pipe;
};

typedef enum {
	e_circular = 0,
	e_block = 1
} t_buffer_method;

class Usb2 : public UsbBase
{
	QueueHandle_t queue;
	SemaphoreHandle_t mutex;

	struct usb_event event;
	int state;
	int irq_count;

    void  (*inputPipeCallBacks[USB2_NUM_PIPES])(uint8_t *buf, int len, void *obj);
    void  *inputPipeObjects[USB2_NUM_PIPES];
    uint8_t   inputPipeBufferMethod[USB2_NUM_PIPES];
    uint16_t   inputPipeCommand[USB2_NUM_PIPES];

    uint32_t *blockBufferBase;
    uint8_t  *circularBufferBase;
    uint8_t free_map[BLOCK_FIFO_ENTRIES];

    uint16_t  prev_status;
    bool  get_fifo(uint16_t *out);
    bool  put_block_fifo(uint16_t in);

    void process_fifo(struct usb_event *, struct usb_packet *);

    int   open_pipe();
    void  init_pipe(int index, struct t_pipe *init);
    void  close_pipe(int pipe);

	void input_task_impl(void);
    void power_off(void);
public:
    Usb2();
    virtual ~Usb2();
    
    static void input_task_start(void *);

    void poll();
    BaseType_t irq_handler(void);
    void init(void);
    void deinit(void);
    void bus_reset();

    uint16_t getSplitControl(int addr, int port, int speed, int type);
    int  control_exchange(struct t_pipe *pipe, void *out, int outlen, void *in, int inlen);
    int  control_write(struct t_pipe *pipe, void *setup_out, int setup_len, void *data_out, int data_len);
    int  allocate_input_pipe(struct t_pipe *pipe, usb_callback callback, void *object);
    void free_input_pipe(int index);
    void pause_input_pipe(int index);
    void resume_input_pipe(int index);

    int  bulk_out(struct t_pipe *pipe, void *buf, int len);
    int  bulk_in(struct t_pipe *pipe, void *buf, int len); // blocking

    void free_input_buffer(int inpipe, uint8_t *buffer);

    // special bootloader function
    UsbDevice *init_simple(void);

    friend class UsbHubDriver;
};

extern Usb2 usb2; // the always existing global

#endif
