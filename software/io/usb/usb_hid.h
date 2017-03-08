#ifndef USB_HID_H
#define USB_HID_H

#include "usb_base.h"
#include "usb_device.h"

class UsbHidDriver : public UsbDriver
{
	int  irq_transaction;

    uint8_t temp_buffer[16];
    
    UsbBase   *host;
    UsbDevice *device;
    UsbInterface *interface;
    int  irq_in;
    bool keyboard;
public:
	static UsbDriver *test_driver(UsbInterface *intf);

	UsbHidDriver(UsbInterface *intf);
	~UsbHidDriver();

	void install(UsbInterface *intf);
	void deinstall(UsbInterface *intf);
	void disable(void);
	void poll(void);
	void pipe_error(int pipe);

    void interrupt_handler(uint8_t *, int);
};

#endif
