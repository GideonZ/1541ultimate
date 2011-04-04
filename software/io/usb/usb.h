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

#define DESCR_DEVICE            0x01
#define DESCR_CONFIGURATION     0x02
#define DESCR_STRING            0x03
#define DESCR_INTERFACE         0x04
#define DESCR_ENDPOINT          0x05
#define DESCR_DEVICE_QUALIFIER  0x06
#define DESCR_OTHER_SPEED       0x07
#define DESCR_INTERFACE_POWER   0x08

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

#define OTG_DP_PD    0x02
#define OTG_DM_PD    0x04
#define OTG_DRV_VBUS 0x20

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

struct t_device_descriptor
{
    BYTE length;
    BYTE type;
    WORD version;
    BYTE device_class;
    BYTE sub_class;
    BYTE protocol;
    BYTE max_packet_size;
    WORD vendor;
    WORD product;
    WORD release;
    BYTE manuf_string;
    BYTE product_string;
    BYTE serial_string;
    BYTE num_configurations;
};

// Eminent 1010:
// 09 02 27 00 01 01 00 80 70 -- max 224 mA
// 09 04 00 00 03 FF FF 00 00 -- interface, 3 endpoints, class ff, sub ff
// 07 05 81 02 00 02 00 -- bulk in      512
// 07 05 02 02 00 02 00 -- bulk out     512
// 07 05 83 03 08 00 01 -- interrupt in 8
 
struct t_device_configuration
{
    BYTE length;
    BYTE type;
    WORD total_length;
    BYTE num_interfaces;
    BYTE config_value;
    BYTE config_string;
    BYTE attributes;
    BYTE max_power;
};

struct t_interface_descriptor
{
    BYTE length;
    BYTE type;
    BYTE interface_number;
    BYTE alternate_setting;
    BYTE num_endpoints;
    BYTE interface_class;
    BYTE sub_class;
    BYTE protocol;
    BYTE interface_string;
};

struct t_endpoint_descriptor
{
    BYTE length;
    BYTE type;
    BYTE endpoint_address;
    BYTE attributes;
    WORD max_packet_size;
    BYTE interval;
};

typedef enum e_dev_state {
    dev_st_invalid = 0,
    dev_st_powered,
    dev_st_address,
    dev_st_configured
} t_dev_state;

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
    int get_device_slot(void);
    DWORD pipes[USB_MAX_PIPES];
    int   transactions[USB_MAX_TRANSACTIONS];
    //ConfigStore *cfg;

    void clean_up(void);
    void clear(void);
    void attach_root(void);
    bool install_device(UsbDevice *dev, bool draws_current);
    void deinstall_device(UsbDevice *dev);

    int  bulk_out_actual(int len, int pipe);
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
    
    void print_status(void);
    int  write_ulpi_register(int, int);
    int  read_ulpi_register(int);
    int  get_ulpi_status(void);
    int  get_speed(void);
    
    void poll(Event &e);
    void init(void);
    void bus_reset(bool);
    void enumerate(void);

    int  create_pipe(int addr, struct t_endpoint_descriptor *epd);
    void free_pipe(int index);
    int  allocate_transaction(int len); // for reoccuring transactions
    void free_transaction(int index);
    
    int  control_exchange(int addr, void *, int, void *, int, BYTE **);
    int  control_write(int addr, void *, int, void *, int);
    
    void unstall_pipe(int pipe);
    int  bulk_out(void *buf, int len, int pipe);
    int  bulk_out_with_prefix(void *prefix, int prefix_len, void *buf, int len, int pipe);
    int  bulk_in(void *buf, int len, int pipe);
    int  interrupt_in(int trans, int pipe, int len, BYTE *buf);
        
    // special bootloader function
    UsbDevice *init_simple(void);

    friend class UsbHubDriver;
};

class UsbDriver
{
public:
	UsbDriver(IndexedList<UsbDriver *> &list);
	UsbDriver() { }
	virtual ~UsbDriver() { }

	virtual UsbDriver *create_instance(void) { return NULL; }
	virtual bool test_driver(UsbDevice *dev) { return false; }
	virtual void install(UsbDevice *dev)     { }
	virtual void deinstall(UsbDevice *dev)   { }
	virtual void poll(void)                  { }
};

extern IndexedList<UsbDriver *> usb_drivers;

class UsbDevice
{
public:
    int current_address;
    enum e_dev_state                device_state;
    struct t_device_descriptor      device_descr;
    struct t_device_configuration   device_config;
    struct t_interface_descriptor   interface_descr;
    struct t_endpoint_descriptor    endpoints[4];
    char manufacturer[32];
    char product[32];
    char serial[32];
    int  pipe_numbers[4];

    Usb       *host;
    UsbDevice *parent;  // in case of being connected to a hub
    UsbDriver *driver;  //

    UsbDevice(Usb *u);
    ~UsbDevice();

    void get_string(int index, char *dest, int len);
    bool get_device_descriptor(bool slow);
    void set_address(int address);
    bool get_configuration(BYTE index);
    void set_configuration(BYTE config);
    bool init(int address);
    int  find_endpoint(BYTE code);
    int  get_max_lun(void);

    // functions that arrange attachment to the system
    void install(void) {
    	if(driver)
    		deinstall();
    	// factory pattern
    	for(int i=0;i<usb_drivers.get_elements();i++) {
    		if(usb_drivers[i]->test_driver(this)) {
    			driver = usb_drivers[i]->create_instance();
    			break;
    		}
    	}
    	if(driver)
    		driver->install(this);
    }

    void deinstall(void)  {
    	if(driver) {
    		driver->deinstall(this);
			delete driver;
    	}
    }
//    int init(void); // gets device descriptor, assigns address
};    

char *unicode_to_ascii(BYTE *in, char *out);

/*
// to be moved to usb_scsi.h:
void usb_scsi_inquiry(struct t_usb_device *dev, BYTE lun);
void usb_scsi_reset(struct t_usb_device *dev);
*/

extern Usb usb; // the always existing global

#endif
