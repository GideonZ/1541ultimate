#ifndef USB_HUB_H
#define USB_HUB_H

#include "usb.h"


class UsbHubDriver : public UsbDriver
{
    int  num_ports;
    int  power_on_time;
    int  hub_current;
    bool individual_power_switch;
    bool individual_overcurrent;
    bool compound;

public:
	UsbHubDriver(IndexedList<UsbDriver *> &list);
	UsbHubDriver();
	~UsbHubDriver();

	UsbHubDriver *create_instance(void);
	bool test_driver(UsbDevice *dev);
	void install(UsbDevice *dev);
	void deinstall(UsbDevice *dev);
	void poll(void);
};

#endif
