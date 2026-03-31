#ifndef USB_HID_H
#define USB_HID_H

#include "usb_base.h"
#include "usb_device.h"
#include "hid_decoder.h"

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
    bool descriptor_keyboard;
    bool descriptor_mouse;
    int16_t mouse_x, mouse_y;
    uint8_t mouse_joy;
    uint8_t keyboard_data[8];
    HidItemList report_items;
    t_item_location rep_button1;
    t_item_location rep_button2;
    t_item_location rep_button3;
    t_item_location rep_mouse_x;
    t_item_location rep_mouse_y;
    t_item_location rep_wheel_v;
    t_item_location rep_wheel_h;
    bool has_button1;
    bool has_button2;
    bool has_button3;
    bool has_wheel_v;
    bool has_wheel_h;
    int wheel_axis_v_remainder;
    int wheel_key_v_remainder;

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
