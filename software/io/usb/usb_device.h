#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include "integer.h"
#include "indexed_list.h"
#include "usb_base.h"

#define DESCR_DEVICE            0x01
#define DESCR_CONFIGURATION     0x02
#define DESCR_STRING            0x03
#define DESCR_INTERFACE         0x04
#define DESCR_ENDPOINT          0x05
#define DESCR_DEVICE_QUALIFIER  0x06
#define DESCR_OTHER_SPEED       0x07
#define DESCR_INTERFACE_POWER   0x08
#define DESCR_HID				0x21
#define DESCR_HID_REPORT		0x22
#define DESCR_CS_INTERFACE		0x24
#define DESCR_CS_ENDPOINT		0x25

extern BYTE c_get_device_descriptor[];

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
	virtual void pipe_error(int pipe)		 { }
	virtual void reset_port(int port)		 { }
};

extern IndexedList<UsbDriver *> usb_drivers;

class UsbDevice
{
public:
    int current_address;
    enum e_dev_state                device_state;
    BYTE *config_descriptor;
    BYTE *hid_descriptor; // should we support more than one?

    //struct t_device_configuration   device_config;
    //struct t_interface_descriptor   interface_descr;
    struct t_endpoint_descriptor    endpoints[4];
    struct t_interface_descriptor *interfaces[4];
    int num_interfaces;

    char manufacturer[32];
    char product[32];
    char serial[32];

    struct t_device_descriptor      device_descr;
    struct t_pipe control_pipe;

    UsbBase   *host;
    int		   speed;
    UsbDevice *parent;  // in case of being connected to a hub
    int        parent_port;
    UsbDriver *driver;

    UsbDevice(UsbBase *u, int speed);
    ~UsbDevice();

    void set_parent(UsbDevice *p, int port) {
    	parent = p;
    	parent_port = port;
    	control_pipe.SplitCtl = host->getSplitControl(parent->current_address, parent_port + 1, speed, 0);
    	//printf("SplitCtl = %4x\n", control_pipe.SplitCtl);
    }

    void device_reset() {
    	if (!parent)
    		host->bus_reset();
    	else
    		parent->driver->reset_port(parent_port);
    }

    void get_string(int index, char *dest, int len);
    void get_pathname(char *dest, int len);
    bool get_device_descriptor();
    struct t_device_configuration *get_device_config();
    void set_address(int address);
    bool get_configuration(BYTE index);
    void set_configuration(BYTE config);
    void unstall_pipe(BYTE ep);

    bool init(int address);
    int  find_endpoint(BYTE code);

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
            driver = NULL;
    	}
    }
//    int init(void); // gets device descriptor, assigns address
};    

char *unicode_to_ascii(BYTE *in, char *out);

#endif
