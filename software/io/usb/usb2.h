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

#define USB2_MAX_DEVICES 16

extern void usb_irq(void);

typedef enum {
	e_circular = 0,
	e_block = 1
} t_buffer_method;

class Usb2 : public UsbBase
{
#ifdef OS
	QueueHandle_t queue;
	SemaphoreHandle_t mutex;

#endif
    void  (*inputPipeCallBacks[USB2_NUM_PIPES])(BYTE *buf, int len, void *obj);
    void  *inputPipeObjects[USB2_NUM_PIPES];
    BYTE   inputPipeBufferMethod[USB2_NUM_PIPES];
    WORD   inputPipeCommand[USB2_NUM_PIPES];

    DWORD *blockBufferBase;
    BYTE  *circularBufferBase;

    BYTE  prev_status;
    bool  get_fifo(WORD *out);
    bool  put_block_fifo(WORD in);

    int   open_pipe();
    void  init_pipe(int index, struct t_pipe *init);
    void  close_pipe(int pipe);

	void input_task_impl(void);
public:
    Usb2();
    virtual ~Usb2();
    
    static void input_task_start(void *);

    void poll(Event &e);
    void irq_handler(void);
    void init(void);
    void deinit(void);
    void bus_reset();

    WORD getSplitControl(int addr, int port, int speed, int type);
    int  control_exchange(struct t_pipe *pipe, void *out, int outlen, void *in, int inlen);
    int  control_write(struct t_pipe *pipe, void *setup_out, int setup_len, void *data_out, int data_len);
    int  allocate_input_pipe(struct t_pipe *pipe, usb_callback callback, void *object);
    void free_input_pipe(int index);
    void pause_input_pipe(int index);
    void resume_input_pipe(int index);

    int  bulk_out(struct t_pipe *pipe, void *buf, int len);
    int  bulk_in(struct t_pipe *pipe, void *buf, int len); // blocking

    void free_input_buffer(int inpipe, BYTE *buffer);

    //    int  create_pipe(int addr, struct t_endpoint_descriptor *epd);
//    void free_pipe(int index);
    
//    int  bulk_out_with_prefix(void *prefix, int prefix_len, void *buf, int len, int pipe);

    // special bootloader function
    UsbDevice *init_simple(void);

    friend class UsbHubDriver;
};

extern Usb2 usb2; // the always existing global

#endif
