#ifndef USB_H
#define USB_H

#include "integer.h"
#include "event.h"
#include "filemanager.h"
#include "indexed_list.h"
#include "config.h"
#include "iomap.h"
#include "usb_base.h"

#define USB_COMMAND_BASE (USB_BASE + 0x800)
#define USB_BUFFER_BASE  (USB_BASE + 0x1000)

#define USB_PIPE(i)         (((volatile DWORD *)(USB_BASE + 0x000))[i])
#define USB_TRANSACTION(i)  (((volatile DWORD *)(USB_BASE + 0x100))[i])
#define USB_BUFFER(i)       *((volatile BYTE *)(USB_BUFFER_BASE + i))
#define USB_COMMAND         *((volatile BYTE *)(USB_COMMAND_BASE + 0x00))
#define USB_RESP_DATA       *((volatile BYTE *)(USB_COMMAND_BASE + 0x00))
#define USB_RESP_STATUS     *((volatile BYTE *)(USB_COMMAND_BASE + 0x01))
#define USB_CMD_QUEUE_STAT  *((volatile BYTE *)(USB_COMMAND_BASE + 0x02))
#define USB_RESP_GET        *((volatile BYTE *)(USB_COMMAND_BASE + 0x03))

#define USB_CMD_GET_STATUS    1
#define USB_CMD_GET_SPEED     2 
#define USB_CMD_GET_DONE      3
#define USB_CMD_BUSRESET_HS   4
#define USB_CMD_BUSRESET_FS   5
#define USB_CMD_DISABLE_HOST  6
#define USB_CMD_ABORT         7
#define USB_CMD_SOF_ENABLE    8
#define USB_CMD_SOF_DISABLE   9
#define USB_CMD_SET_GAP	     10
#define USB_CMD_SET_BUSY     11
#define USB_CMD_CLEAR_BUSY	 12
#define USB_CMD_SET_DEBUG    13
#define USB_CMD_SCAN_DISABLE 14
#define USB_CMD_SCAN_ENABLE  15

#define USB_RESP_REGVALUE   0x01
#define USB_RESP_AVAILABLE  0x80
#define USB_CMD_QUEUE_FULL  0x80

#define USB_MAX_DEVICES      16
#define USB_MAX_TRANSACTIONS 64
#define USB_MAX_PIPES        30

#define ULPI_VENDOR_ID_L  0x00
#define ULPI_FUNC_CTRL    0x04
#define ULPI_OTG_CONTROL  0x0A
#define ULPI_IRQEN_RISING 0x0D
#define ULPI_IRQ_CURRENT  0x13
#define ULPI_IRQ_STATUS   0x14
#define ULPI_LINE_STATE   0x15

#define OTG_DP_PD     0x02
#define OTG_DM_PD     0x04
#define OTG_DRV_VBUS  0x20
#define OTG_DISCHARGE 0x08

#define FUNC_XCVR_HS   0x00
#define FUNC_XCVR_FS   0x01
#define FUNC_XCVR_LS   0x02
#define FUNC_XCVR_FSLS 0x03
#define FUNC_TERM_SEL  0x04
#define FUNC_RESET     0x20
#define FUNC_SUSPENDM  0x40

#define IRQ_HOST_DISCONNECT 0x01

#define STAT_LINESTATE_0   0x01
#define STAT_LINESTATE_1   0x02
#define STAT_LINESTATE	   0x03
#define STAT_VBUS_BITS	   0x0C
#define STAT_VBUS_LOW      0x00
#define STAT_VBUS_SESS_END 0x04
#define STAT_VBUS_SESS_VLD 0x08
#define STAT_VBUS_VLD	   0x0C

class UsbDevice;
class UsbDriver;
   
class Usb1 : public UsbBase
{
	int gap;
    int poll_delay;
	int se0_seen;
    DWORD pipes[USB_MAX_PIPES];
    int   transactions[USB_MAX_TRANSACTIONS];
    void  (*callbacks[USB_MAX_TRANSACTIONS])(BYTE *buf, int len, void *obj);
    void  *objects[USB_MAX_TRANSACTIONS];
    int   input_pipe_numbers[USB_MAX_TRANSACTIONS];

    int  write_ulpi_register(int, int);
    int  read_ulpi_register(int);
    int  get_ulpi_status(void);
    int  get_speed(void);

    //ConfigStore *cfg;

    void clear(void);

    int  bulk_out_actual(int len, int pipe);
    int  interrupt_in(int trans, int pipe, int len);
public:
    bool debug;
    int speed;
    int max_current;
    int remaining_current;

    bool initialized;
    bool device_present;
    
    Usb1();
    virtual ~Usb1();
    
    void print_status(void);
    
    void poll(Event &e);
    void init(void);
    void bus_reset();

    void attach_root(void);
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

extern Usb1 usb1; // the always existing global

#endif
