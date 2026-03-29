#ifndef USB_HID_LOGITECH_H
#define USB_HID_LOGITECH_H

#include "usb_base.h"
#include "usb_device.h"

class UsbHidLogitechDriver : public UsbDriver
{
	int  irq_transaction;
    struct t_pipe ipipe;

    uint8_t irq_data[64];

    UsbBase   *host;
    UsbDevice *device;
    int16_t mouse_x, mouse_y;

public:
	static bool test_interface(UsbInterface *intf);
	static UsbDriver *test_driver(UsbInterface *intf);

	UsbHidLogitechDriver(UsbInterface *intf);
	~UsbHidLogitechDriver();

	void install(UsbInterface *intf);
	void deinstall(UsbInterface *intf);
	void disable(void);
	void poll(void);
	void pipe_error(int pipe);

    void interrupt_handler();
};

#endif
