#ifndef USB_H
#define USB_H

#include "integer.h"
#include "event.h"
#include "filemanager.h"
#include "indexed_list.h"
#include "config.h"

#define USB_PIPE(i)         (((volatile DWORD *)0x4080000)[i])
#define USB_TRANSACTION(i)  (((volatile DWORD *)0x4080100)[i])
#define USB_BUFFER(i)       *((volatile BYTE *)(0x4081000 + i))
#define USB_COMMAND         *((volatile BYTE *)0x4080800)
#define USB_RESP_DATA       *((volatile BYTE *)0x4080800)
#define USB_RESP_STATUS     *((volatile BYTE *)0x4080801)
#define USB_CMD_QUEUE_STAT  *((volatile BYTE *)0x4080802)
#define USB_RESP_GET        *((volatile BYTE *)0x4080803)

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
   
#ifndef BOOTLOADER
class Usb : public ConfigurableObject
{
#else
class Usb
{
    ConfigStore *cfg;
#endif
    int gap;
    int poll_delay;
	int se0_seen;
    DWORD pipes[USB_MAX_PIPES];
    int   transactions[USB_MAX_TRANSACTIONS];
    void  (*callbacks[USB_MAX_TRANSACTIONS])(BYTE *buf, int len, void *obj);
    void  *objects[USB_MAX_TRANSACTIONS];
    int   input_pipe_numbers[USB_MAX_TRANSACTIONS];

    //ConfigStore *cfg;

    void clean_up(void);
    void clear(void);
    void attach_root(void);
    bool install_device(UsbDevice *dev, bool draws_current);
    void deinstall_device(UsbDevice *dev);

    int  bulk_out_actual(int len, int pipe);
    int  interrupt_in(int trans, int pipe, int len);
public:
    bool debug;
    int speed;
    int max_current;
    int remaining_current;

    bool initialized;
    bool device_present;
    UsbDevice *device_list[USB_MAX_DEVICES];
    UsbDevice *first_device(void);
    
    Usb();
    ~Usb();
    
    int get_device_slot(void);
    void print_status(void);
    int  write_ulpi_register(int, int);
    int  read_ulpi_register(int);
    int  get_ulpi_status(void);
    int  get_speed(void);
    
    void poll(Event &e);
    void init(void);
    void bus_reset(bool);
//    void enumerate(void);

    int  create_pipe(int addr, struct t_endpoint_descriptor *epd);
    void free_pipe(int index);
    int  allocate_input_pipe(int len, int pipe, void(*callback)(BYTE *buf, int len, void *obj), void *object); // for reoccuring transactions
    void free_input_pipe(int index);
    
    int  control_exchange(int addr, void *, int, void *, int, BYTE **);
    int  control_write(int addr, void *, int, void *, int);
    
    void unstall_pipe(int pipe);
    int  bulk_out(void *buf, int len, int pipe);
    int  bulk_out_with_prefix(void *prefix, int prefix_len, void *buf, int len, int pipe);
    int  bulk_in(void *buf, int len, int pipe); // blocking

//    int  start_bulk_in(int trans, int pipe, int len);
//    int  transaction_done(int trans);
//    int  get_bulk_in_data(int trans, BYTE **buf);    
//    BYTE *get_bulk_out_buffer(int pipe);
        
    // special bootloader function
    UsbDevice *init_simple(void);

    friend class UsbHubDriver;
    friend class Usb2;
};

extern Usb usb; // the always existing global

#endif
