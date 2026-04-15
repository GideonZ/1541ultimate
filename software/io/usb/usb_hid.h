#ifndef USB_HID_H
#define USB_HID_H

#include "usb_base.h"
#include "usb_device.h"
#include "hid_decoder.h"

struct t_usb_hid_status_snapshot
{
    char mouse_name[33];
    char mouse_mode[12];
    char keyboard_name[33];
    char keyboard_mode[12];
};

void usb_hid_get_status_snapshot(t_usb_hid_status_snapshot& snapshot);

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
    int native_wheel_delta_queue[8];
    uint8_t native_wheel_queue_head;
    uint8_t native_wheel_queue_tail;
    uint8_t native_wheel_base_joy;
    uint8_t native_wheel_output_active;
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
    bool previous_left_button_pressed;
    bool mouse_registered;
    bool menu_left_button_consumed;
    bool menu_right_button_consumed;
    int menu_wheel_h_latch;
    int menu_wheel_v_latch;
    int menu_wheel_v_mode;
    int menu_wheel_v_burst_accumulator;
    int menu_wheel_v_burst_direction;
    uint32_t menu_wheel_v_last_tick;
    int wheel_axis_h_remainder;
    int wheel_axis_v_remainder;
    int wheel_key_h_remainder;
    int wheel_key_v_remainder;
    int wheel_step_accumulator;
    int wheel_pulse_phase;
    uint8_t wheel_pulse_mask;
    uint32_t wheel_pulse_next_tick;
    int wheel_pulse_burst_direction;
    uint8_t wheel_pulse_burst_count;
    int pointer_sensitivity_setting;
    int pointer_sensitivity_remainder_x;
    int pointer_sensitivity_remainder_y;
    int adaptive_accel_ema_x16;
    int adaptive_accel_scale_factor;

public:
	static UsbDriver *test_driver(UsbInterface *intf);

	UsbHidDriver(UsbInterface *intf);
	~UsbHidDriver();

    bool has_active_report_keyboard(void) const;
    bool has_active_report_mouse(void) const;
    void relinquish_boot_function(bool release_keyboard, bool release_mouse);

	void install(UsbInterface *intf);
	void deinstall(UsbInterface *intf);
	void disable(void);
	void poll(void);
	void pipe_error(int pipe);

    void interrupt_handler();
};

#endif
