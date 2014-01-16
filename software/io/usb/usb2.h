#ifndef USB2_H
#define USB2_H

#include "integer.h"
#include "event.h"
#include "filemanager.h"
#include "indexed_list.h"
#include "config.h"
#include "usb.h"
#include "usb_device.h"

#define USB2_MAX_DEVICES      16
#define USB2_MAX_TRANSACTIONS 64
#define USB2_MAX_PIPES        30

#define HW_USB2_CODE_BE  0x5080000
#define USB2_CODE_BASE   0x4080000
#define USB2_REGISTERS   0x4081000

#define USB2_NANO_ENABLE *((volatile BYTE *)(USB2_REGISTERS + 0x0))

#define USB2_ATTR_FIFO_DATA8(x)  (*((volatile BYTE *)(USB2_CODE_BASE + 0x660 + x)))
#define USB2_BLOCK_FIFO_DATA8(x) (*((volatile BYTE *)(USB2_CODE_BASE + 0x760 + x)))

//#define USB2_ATTR_FIFO_DATA(x)   (DWORD)USB2_ATTR_FIFO_DATA8(x<<1) | (DWORD)(USB2_ATTR_FIFO_DATA8((x<<1)+1)) << 8
//#define USB2_BLOCK_FIFO_DATA(x)  (DWORD)USB2_BLOCK_FIFO_DATA8(x<<1) | (DWORD)(USB2_BLOCK_FIFO_DATA8((x<<1)+1)) << 8

#define USB2_ATTR_FIFO_MASK  0x7F
#define USB2_BLOCK_FIFO_MASK 0x3F

#define USB2_ATTR_FIFO_TAIL  *((volatile BYTE *)(USB2_CODE_BASE + 0x7F8)) // write
#define USB2_ATTR_FIFO_HEAD  *((volatile BYTE *)(USB2_CODE_BASE + 0x7FA))
#define USB2_BLOCK_FIFO_TAIL *((volatile BYTE *)(USB2_CODE_BASE + 0x7FC))
#define USB2_BLOCK_FIFO_HEAD *((volatile BYTE *)(USB2_CODE_BASE + 0x7FE)) // write

#define USB2_CMD_SETUP    0x21
#define USB2_CMD_DATA_OUT 0x22
#define USB2_CMD_DATA_IN  0x23

#define USB2_EVENT_HALT  0x8001
#define USB2_EVENT_RETRY 0x8002
#define USB2_EVENT_DONE  0x8003

#define USB2_EVENT_NO_DEVICE  0x8011
#define USB2_EVENT_ATTACH     0x8012
#define USB2_EVENT_DISCONNECT 0x8013

#define USB2_CMD_CMD          *((volatile BYTE *)(USB2_CODE_BASE + 0x7E0))
#define USB2_CMD_MEM_ADDR     *((volatile DWORD *)(USB2_CODE_BASE + 0x7E4))
#define USB2_CMD_DEV_ADDR     *((volatile DWORD *)(USB2_CODE_BASE + 0x7E8)) // also writes toggle state (0x800)
#define USB2_CMD_LENGTH       *((volatile DWORD *)(USB2_CODE_BASE + 0x7EC))
#define USB2_CMD_TRANSFERRED  *((volatile DWORD *)(USB2_CODE_BASE + 0x7F0))


class UsbDevice;
class UsbDriver;
   
class Usb2 : public Usb
{
    void clean_up(void);
    void attach_root(void);
    bool install_device(UsbDevice *dev, bool draws_current);
    void deinstall_device(UsbDevice *dev);

    bool get_fifo(DWORD *);

public:
    bool initialized;
    bool device_present;
    UsbDevice *device_list[USB2_MAX_DEVICES];
    UsbDevice *first_device(void);
    
    Usb2();
    ~Usb2();
    
    void poll(Event &e);
    void init(void);

    int  create_pipe(int addr, struct t_endpoint_descriptor *epd);
    void free_pipe(int index);
//    int  allocate_transaction(int len); // for reoccuring transactions
//    void free_transaction(int index);
    
    int  control_exchange(int addr, void *, int, void *, int, BYTE **);
    int  control_write(int addr, void *, int, void *, int);
    
    void unstall_pipe(int pipe);
    int  bulk_out(void *buf, int len, int pipe);
    int  bulk_out_with_prefix(void *prefix, int prefix_len, void *buf, int len, int pipe);
    int  bulk_in(void *buf, int len, int pipe); // blocking
    int  interrupt_in(int trans, int pipe, int len, BYTE *buf);

//    int  start_bulk_in(int trans, int pipe, int len);
//    int  transaction_done(int trans);
//    int  get_bulk_in_data(int trans, BYTE **buf);    
//    BYTE *get_bulk_out_buffer(int pipe);
//    int  bulk_out_actual(int len, int pipe);
        
    // special bootloader function
    UsbDevice *init_simple(void);

    friend class UsbHubDriver;
};

extern Usb2 usb2; // the always existing global

#endif
