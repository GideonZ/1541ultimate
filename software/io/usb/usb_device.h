#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include "integer.h"
#include "indexed_list.h"
#include "usb_base.h"
#include "factory.h"
#include <string.h>

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
    uint8_t dummy1;
    uint8_t dummy2;
};

struct t_hid_descriptor
{
    uint8_t length;
    uint8_t type;
    uint16_t hid_version;
    uint8_t country_code;
    uint8_t number_of_descriptors;
    uint8_t descriptor_type;
    uint16_t descriptor_length;
} __attribute__((packed));

typedef enum e_dev_state {
    dev_st_invalid = 0,
    dev_st_powered,
    dev_st_address,
    dev_st_configured
} t_dev_state;

class UsbDevice;
class UsbInterface;

class UsbDriver
{
public:
	UsbDriver(UsbInterface *intf) { }
	virtual ~UsbDriver() { }

	virtual UsbDevice  *getDevice(void) 		 { return NULL; }
	virtual void install(UsbInterface *intf)     { }
	virtual void deinstall(UsbInterface *intf)   { }
	virtual void poll(void)                  { }
	virtual void pipe_error(int pipe)		 { }
	virtual void reset_port(int port)		 { }
	virtual void disable(void)				 { }
};

class UsbInterface
{
    uint8_t *hid_report_descriptor;
    uint8_t  interface_number;
    struct t_interface_descriptor   interface_descr;
    struct t_endpoint_descriptor    endpoints[4];
    struct t_hid_descriptor			hid_descriptor;
    bool hid_descriptor_valid;
    bool enabled;
    int numEndpoints;
    UsbDriver *driver;
    UsbDevice *parentDevice;
    UsbInterface *alternative;

public:
    static Factory<UsbInterface *, UsbDriver *>* getUsbDriverFactory() {
		static Factory<UsbInterface *, UsbDriver *> usb_driver_factory;
		return &usb_driver_factory;
	}

    UsbInterface(UsbDevice *dev, int interfaceNr, struct t_interface_descriptor *desc) {
    	parentDevice = dev;
    	interface_number = interfaceNr;
    	hid_report_descriptor = NULL;
    	driver = NULL;
    	alternative = NULL;
    	numEndpoints = 0;
    	hid_descriptor_valid = false;
    	enabled = false;
    	memcpy(&interface_descr, desc, sizeof(struct t_interface_descriptor));
    }

    ~UsbInterface() {
    	if (alternative) {
    		delete alternative;
    	}
    	if (hid_report_descriptor) {
    		delete hid_report_descriptor;
    	}
    }


    void addAlternative(UsbInterface *intf) {
    	alternative = intf;
    }

    void addEndpoint(uint8_t *data, int len) {
    	if (numEndpoints < 4) {
    		struct t_endpoint_descriptor *ep = &endpoints[numEndpoints];
    		memcpy((void *)ep, (void *)data, len);
    		ep->max_packet_size = (uint16_t(data[5]) << 8) | data[4];
    		//printf("Endpoint found with address %b, attr: %b, maxpkt: %04x\n",
        	//		ep->endpoint_address, ep->attributes, ep->max_packet_size);
    		numEndpoints++;
    	}
    }

    void setHidDescriptor(uint8_t *data, int len) {
    	memcpy(&hid_descriptor, data, len);
    	hid_descriptor_valid = true;
    }

    void getHidReportDescriptor(void);

    void setDriver(UsbDriver *drv) {
    	driver = drv;
    }

    UsbDriver *getDriver(void) {
    	return driver;
    }

    UsbDevice *getParentDevice(void) {
    	return parentDevice;
    }

    const struct t_interface_descriptor *getInterfaceDescriptor(void) {
    	return &interface_descr;
    }

    void poll(void) {
    	if (enabled) {
    		if (driver) {
    			driver->poll();
    		}
    	}
    }

    void install(void) {
		getHidReportDescriptor();
    	UsbDriver *driver = getUsbDriverFactory()->create(this);
    	setDriver(driver);
    	if (driver) {
			driver->install(this);
			enabled = true;
    	} else {
    		printf("No driver found for interface %d\n", this->interface_number);
    	}
    }

    // Called only from the poll/cleanup context
    void deinstall(void)  {
    	enabled = false;
    	if(driver) {
    		driver->deinstall(this);
			delete driver;
            driver = NULL;
    	}
    }

    void disable(void) {
    	enabled = false;
    	if(driver) {
    		driver->disable();
    	}
    }

    struct t_endpoint_descriptor *find_endpoint(uint8_t code)
    {
        uint8_t ep_code;

        for(int i=0;i<4;i++) {
            if(endpoints[i].length) {
                ep_code = (endpoints[i].attributes & 0x03) |
                          (endpoints[i].endpoint_address & 0x80);
                if (ep_code == code)
                    return &endpoints[i];
            }
        }
        return 0;
    }
};

class UsbDevice
{
public:
	int current_address;
    enum e_dev_state                device_state;
    uint8_t *config_descriptor;

    //struct t_device_configuration   device_config;
    struct t_device_descriptor      device_descr;
    int num_interfaces;
    UsbInterface *interfaces[4]; // we support composite devices with up to 4 interfaces

    uint16_t vendorID;
    uint16_t productID;

    char manufacturer[32];
    char product[32];
    char serial[32];
    uint8_t language[2];

    UsbBase   *host;
    int		   speed;
    UsbDevice *parent;  // in case of being connected to a hub
    int        parent_port;
    bool	   disabled;
    struct t_pipe control_pipe;

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
    		parent->interfaces[0]->getDriver()->reset_port(parent_port);
    }

    // Called during init, from the Event context
    void disable(void);
    void get_language(void);
    void get_string(int index, char *dest, int len);
    char *get_pathname(char *dest, int len);
    bool get_device_descriptor();
    struct t_device_configuration *get_device_config();
    void set_address(int address);
    bool get_configuration(uint8_t index);
    void set_configuration(uint8_t config);
    void set_interface(uint8_t intf, uint8_t alt);

    int  unstall_pipe(uint8_t ep);

    // Called only from the Event context
    bool init(int address);
    bool init2(void);

    struct t_endpoint_descriptor *find_endpoint(uint8_t code);

    // functions that arrange attachment to the system
    // Called only from the Event context
    void install(void) {
    	for (int i = 0; i < num_interfaces; i++) {
    		if (interfaces[i]) {
    			interfaces[i]->install();
    		}
    	}
    }

    void poll(void) {
    	for (int i = 0; i < num_interfaces; i++) {
    		if (interfaces[i]) {
    			interfaces[i]->poll();
    		}
    	}
    }

    // Called only from the poll/cleanup context
    void deinstall(void)  {
    	for (int i = 0; i < num_interfaces; i++) {
    		if (interfaces[i]) {
				interfaces[i]->deinstall();
    		}
    	}
    }

};    

char *unicode_to_ascii(uint8_t *in, char *out);

#endif
