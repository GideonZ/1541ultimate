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

struct t_pipe {
    UsbDevice *device;
    char     name[8];
    uint16_t Command;
	uint16_t DevEP;
	uint16_t Length;
	uint16_t MaxTrans;
	uint16_t Interval;
	uint16_t SplitCtl;
	uint16_t needPing;
	uint16_t highSpeed;
	void *buffer;
};

typedef enum {
	e_control = 0,
	e_isochronous,
	e_bulk,
	e_interrupt
} t_endpoint_type;

typedef void (*usb_callback)(void *obj);

#define BLOCK_FIFO_ENTRIES 64

class UsbBase
{
    int bus_speed;
    uint8_t deviceUsed[128];
    uint8_t *setupBuffer;

	QueueHandle_t queue;
	QueueHandle_t cleanup_queue;
	SemaphoreHandle_t mutex;
	SemaphoreHandle_t commandSemaphore;
	volatile bool enumeration_lock;

	int irq_count;

    void  (*inputPipeCallBacks[USB2_NUM_PIPES])(void *obj);
    void  *inputPipeObjects[USB2_NUM_PIPES];
    struct t_pipe *inputPipeDefinitions[USB2_NUM_PIPES];

    uint16_t  prev_status;
    bool  get_fifo(uint16_t *out);
    bool  put_block_fifo(uint16_t in);
    void process_fifo(uint16_t);
    void handle_status(uint16_t status);
    uint16_t complete_command(int timeout);

    int   open_pipe();
    void  activate_autopipe(int index, struct t_pipe *init);
    void  close_pipe(int pipe);

    static void input_task_start(void *);
	void input_task_impl(void);
    void power_off(void);

    void deinitHardware(void);
    int get_device_slot(void);
    void deinstall_device(UsbDevice *dev);
    void doPing(struct t_pipe *);
public:
    int max_current;
    int remaining_current;
    bool initialized;

    UsbDevice *rootDevice;

    UsbBase();
    virtual ~UsbBase();
    
    void initHardware(void);
    BaseType_t irq_handler(void);
    UsbDevice *first_device(void);

    void clean_up(void);
    void attach_root(void);
    bool init_device(UsbDevice *dev);
    bool install_device(UsbDevice *dev, bool draws_current);

    void queueDeinstall(UsbDevice *device);

    void set_bus_speed(int sp) { bus_speed = sp; }
    int  get_bus_speed(void)   { return bus_speed; }

    void poll();
    void init(void);
    void deinit(void);
    void bus_reset();

    uint16_t getSplitControl(int addr, int port, int speed, int type);
    int  getReceivedLength(int index);
    int  control_exchange(struct t_pipe *pipe, void *out, int outlen, void *in, int inlen);
    int  control_write(struct t_pipe *pipe, void *setup_out, int setup_len, void *data_out, int data_len);
    int  allocate_input_pipe(struct t_pipe *pipe, usb_callback callback, void *object);
    void initialize_pipe(struct t_pipe *pipe, UsbDevice *device, struct t_endpoint_descriptor *ep);
    void free_input_pipe(int index);
    void pause_input_pipe(int index);
    void resume_input_pipe(int index);

    int  bulk_out(struct t_pipe *pipe, void *buf, int len, int timeout = 20000);
    int  bulk_in(struct t_pipe *pipe, void *buf, int len, int timeout = 20000);

    // special bootloader function
    UsbDevice *init_simple(void);

    friend class UsbHubDriver;
};

#include "usb_device.h"

extern UsbBase usb2;
extern void cache_load(uint8_t *buffer, int length);
#endif
