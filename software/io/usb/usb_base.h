#ifndef USB_BASE_H
#define USB_BASE_H

#include "integer.h"
#include "event.h"
#include "indexed_list.h"
#include "poll.h"

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

class UsbDevice;
class UsbDriver;
   
#define USB_MAX_DEVICES 16

//#ifndef BOOTLOADER
//class UsbBase : public ConfigurableObject
//{
//#else
class UsbBase
{
//    ConfigStore *cfg;
//#endif
    int get_device_slot(void);
    int bus_speed;
public:
    int max_current;
    int remaining_current;
    bool initialized;
    bool device_present;

    UsbDevice *device_list[USB_MAX_DEVICES];

    UsbBase();
    virtual ~UsbBase();
    
    UsbDevice *first_device(void);

    void clean_up(void);
    virtual void attach_root(void);
    bool install_device(UsbDevice *dev, bool draws_current);
    void deinstall_device(UsbDevice *dev);

    void set_bus_speed(int sp) { bus_speed = sp; }
    int  get_bus_speed(void)   { return bus_speed; }

    virtual void poll() { }
    virtual void init(void) { }
    virtual void deinit(void) { }
    virtual void bus_reset() { }
    virtual uint16_t getSplitControl(int addr, int port, int speed, int type) { return 0; }
    virtual int  control_exchange(struct t_pipe *pipe, void *out, int outlen, void *in, int inlen) { return -1; }
    virtual int  control_write(struct t_pipe *pipe, void *setup_out, int setup_len, void *data_out, int data_len) { return -1; }
    virtual int  allocate_input_pipe(struct t_pipe *pipe, usb_callback callback, void *object) { return -1; }
    virtual void free_input_pipe(int index) {}
    virtual void pause_input_pipe(int index) {}
    virtual void resume_input_pipe(int index) {}

    virtual int  bulk_out(struct t_pipe *pipe, void *buf, int len) { return -1; };
    virtual int  bulk_in(struct t_pipe *pipe, void *buf, int len) { return -1; }; // blocking

    virtual void free_input_buffer(int inpipe, uint8_t *buffer) { }

    virtual int  create_pipe(int addr, struct t_endpoint_descriptor *epd) { return -1; }
    virtual void free_pipe(int index) {}
    
    //int  bulk_out_with_prefix(void *prefix, int prefix_len, void *buf, int len, int pipe);

    // special bootloader function
    virtual UsbDevice *init_simple(void) { }

    friend class UsbHubDriver;
};

#endif
