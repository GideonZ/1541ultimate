#ifndef USB_BASE_H
#define USB_BASE_H

#include "integer.h"
#include "indexed_list.h"

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

typedef enum {
	e_circular = 0,
	e_block = 1
} t_buffer_method;

struct t_pipe {
	uint16_t Command;
	uint16_t DevEP;
	uint16_t Length;
	uint16_t MaxTrans;
	uint16_t Interval;
	uint16_t SplitCtl;
};

typedef enum {
	e_control = 0,
	e_isochronous,
	e_bulk,
	e_interrupt
} t_endpoint_type;

typedef void (*usb_callback)(uint8_t *buf, int len, void *obj);

class UsbBase
{
    int bus_speed;
    uint8_t deviceUsed[128];

	QueueHandle_t queue;
	QueueHandle_t cleanup_queue;
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
    void process_fifo(struct usb_event *);
    void handle_status(uint16_t status);

    int   open_pipe();
    void  init_pipe(int index, struct t_pipe *init);
    void  close_pipe(int pipe);

    static void input_task_start(void *);
	void input_task_impl(void);
    void power_off(void);

    void initHardware(void);
    void deinitHardware(void);
    int get_device_slot(void);
    void deinstall_device(UsbDevice *dev);
public:
    int max_current;
    int remaining_current;
    bool initialized;
    bool device_present;

    UsbDevice *rootDevice;

    UsbBase();
    virtual ~UsbBase();
    
    BaseType_t irq_handler(void);
    UsbDevice *first_device(void);

    void clean_up(void);
    void attach_root(void);
    bool install_device(UsbDevice *dev, bool draws_current);
    void queueDeinstall(UsbDevice *device);

    void set_bus_speed(int sp) { bus_speed = sp; }
    int  get_bus_speed(void)   { return bus_speed; }

    void poll();
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

    int  create_pipe(int addr, struct t_endpoint_descriptor *epd);
    void free_pipe(int index);
    
    // special bootloader function
    UsbDevice *init_simple(void);

    friend class UsbHubDriver;
};

#include "usb_device.h"

extern UsbBase usb2;

#endif
