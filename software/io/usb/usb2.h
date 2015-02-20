#ifndef USB2_H
#define USB2_H

#include "___usb.h"
#include "integer.h"
#include "event.h"
#include "filemanager.h"
#include "indexed_list.h"
#include "config.h"
#include "usb_device.h"
#include "usb_nano.h"

class UsbDevice;
class UsbDriver;

#define USB2_MAX_DEVICES 16

class Usb2 : public UsbBase
{
    void  (*inputPipeCallBacks[USB2_NUM_PIPES])(BYTE *buf, int len, void *obj);
    void  *inputPipeObjects[USB2_NUM_PIPES];
    BYTE  prev_status;
    bool  get_fifo(WORD *out);
    int   open_pipe();
    void  init_pipe(int index, struct t_pipe *init);
    void  close_pipe(int pipe);

public:
    Usb2();
    virtual ~Usb2();
    
    void poll(Event &e);
    void init(void);
    void bus_reset();

    int  control_exchange(struct t_pipe *pipe, void *out, int outlen, void *in, int inlen);
    int  control_write(struct t_pipe *pipe, void *setup_out, int setup_len, void *data_out, int data_len);
    int  allocate_input_pipe(struct t_pipe *pipe, void(*callback)(BYTE *buf, int len, void *obj), void *object);
    void free_input_pipe(int index);
    void unstall_pipe(struct t_pipe *);
    int  bulk_out(struct t_pipe *pipe, void *buf, int len);
    int  bulk_in(struct t_pipe *pipe, void *buf, int len); // blocking

//    int  create_pipe(int addr, struct t_endpoint_descriptor *epd);
//    void free_pipe(int index);
    
//    int  bulk_out_with_prefix(void *prefix, int prefix_len, void *buf, int len, int pipe);

    // special bootloader function
    UsbDevice *init_simple(void);

    friend class UsbHubDriver;
};

extern Usb2 usb2; // the always existing global

#endif
