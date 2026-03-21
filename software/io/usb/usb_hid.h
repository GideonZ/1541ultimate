#ifndef USB_HID_H
#define USB_HID_H

#include "usb_base.h"
#include "usb_device.h"

#if USE_HID_REPORT
#   include "hid_decoder.h"
#endif

class UsbHidDriver : public UsbDriver
{
	int  irq_transaction;
    struct t_pipe ipipe;

    uint8_t irq_data[64];
    
    UsbBase   *host;
    UsbDevice *device;
    UsbInterface *interface;
    int  irq_in;
    bool keyboard;
    bool mouse;
    int16_t mouse_x, mouse_y;
#if USE_HID_REPORT
    t_item_location rep_button1;
    t_item_location rep_button2;
    t_item_location rep_button3;
    t_item_location rep_mouse_x;
    t_item_location rep_mouse_y;
#endif

public:
	static UsbDriver *test_driver(UsbInterface *intf);

	UsbHidDriver(UsbInterface *intf);
	~UsbHidDriver();

	void install(UsbInterface *intf);
	void deinstall(UsbInterface *intf);
	void disable(void);
	void poll(void);
	void pipe_error(int pipe);

    void interrupt_handler();
};

#endif
