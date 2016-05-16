#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include "integer.h"
#include "indexed_list.h"
#include "usb_base.h"
#include "factory.h"

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

extern uint8_t c_get_device_descriptor[];

struct t_device_descriptor
{
    uint8_t length;
    uint8_t type;
    uint16_t version;
    uint8_t device_class;
    uint8_t sub_class;
    uint8_t protocol;
    uint8_t max_packet_size;
    uint16_t vendor;
    uint16_t product;
    uint16_t release;
    uint8_t manuf_string;
    uint8_t product_string;
    uint8_t serial_string;
    uint8_t num_configurations;
};

// Eminent 1010:
// 09 02 27 00 01 01 00 80 70 -- max 224 mA
// 09 04 00 00 03 FF FF 00 00 -- interface, 3 endpoints, class ff, sub ff
// 07 05 81 02 00 02 00 -- bulk in      512
// 07 05 02 02 00 02 00 -- bulk out     512
// 07 05 83 03 08 00 01 -- interrupt in 8
 
struct t_device_configuration
{
    uint8_t length;
    uint8_t type;
    uint16_t total_length;
    uint8_t num_interfaces;
    uint8_t config_value;
    uint8_t config_string;
    uint8_t attributes;
    uint8_t max_power;
};

struct t_interface_descriptor
{
    uint8_t length;
    uint8_t type;
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t num_endpoints;
    uint8_t interface_class;
    uint8_t sub_class;
    uint8_t protocol;
    uint8_t interface_string;
};

struct t_endpoint_descriptor
{
    uint8_t length;
    uint8_t type;
    uint8_t endpoint_address;
    uint8_t attributes;
    uint16_t max_packet_size;
    uint8_t interval;
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
	UsbDriver() { }
	virtual ~UsbDriver() { }

	virtual void install(UsbDevice *dev)     { }
	virtual void deinstall(UsbDevice *dev)   { }
	virtual void poll(void)                  { }
	virtual void pipe_error(int pipe)		 { }
	virtual void reset_port(int port)		 { }
	virtual void disable(void)				 { }
};

class UsbDevice
{
public:
    static Factory<UsbDevice *, UsbDriver *>* getUsbDriverFactory() {
		static Factory<UsbDevice *, UsbDriver *> usb_driver_factory;
		return &usb_driver_factory;
	}

	int current_address;
    enum e_dev_state                device_state;
    uint8_t *config_descriptor;
    uint8_t *hid_descriptor; // should we support more than one?

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
    bool	   disabled;

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

    // Called during init, from the Event context
    void disable(void);
    void get_string(int index, char *dest, int len);
    void get_pathname(char *dest, int len);
    bool get_device_descriptor();
    struct t_device_configuration *get_device_config();
    void set_address(int address);
    bool get_configuration(uint8_t index);
    void set_configuration(uint8_t config);
    void set_interface(uint8_t intf);

    void unstall_pipe(uint8_t ep);

    // Called only from the Event context
    bool init(int address);
    struct t_endpoint_descriptor *find_endpoint(uint8_t code);

    // functions that arrange attachment to the system
    // Called only from the Event context
    void install(void) {
    	if(driver)
    		deinstall();
    	driver = getUsbDriverFactory()->create(this);
    	if(driver)
    		driver->install(this);
    }

    // Called only from the poll/cleanup context
    void deinstall(void)  {
    	if(driver) {
    		driver->deinstall(this);
			delete driver;
            driver = NULL;
    	}
    }

    void poll(void) {
    	if (!disabled) {
    		if (driver) {
    			driver->poll();
    		}
    	}
    }
};    

char *unicode_to_ascii(uint8_t *in, char *out);

#endif
