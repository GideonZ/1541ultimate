#ifndef USB_HUB_H
#define USB_HUB_H

#include "usb_base.h"
#include "usb_device.h"

class UsbHubDriver : public UsbDriver
{
    int  num_ports;
    int  power_on_time;
    int  hub_current;
    bool individual_power_switch;
    bool individual_overcurrent;
    bool compound;

    int  irq_transaction;
    int  port_in_reset;
    int  reset_timeout;
    uint8_t buf[64];
    uint8_t dummy[16];
    volatile uint8_t irq_data[4];
    
    UsbBase   *host;
    UsbDevice *device;
    UsbDevice *children[7];

public:
	static UsbDriver *test_driver(UsbInterface *intf);

	UsbHubDriver(UsbInterface *intf);
	~UsbHubDriver();

	void disable(void);
	void install(UsbInterface *intf);
	void deinstall(UsbInterface *intf);
	void poll(void);
	void pipe_error(int pipe);
	void reset_port(int port);

	void interrupt_handler(uint8_t *, int);

	UsbDevice *getChild(int index) {
		if (index < 0) {
			return 0;
		}
		if (index > 6) {
			return 0;
		}
		return children[index];
	}
};

#endif
