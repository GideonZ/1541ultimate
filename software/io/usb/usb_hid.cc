#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "itu.h"
#include "integer.h"
#include "usb_hid.h"
#include "profiler.h"
#include "FreeRTOS.h"
#include "task.h"
#include "keyboard_usb.h"
#include "u64.h"

// Standards-based HID support only. Devices must expose standard HID mouse or
// keyboard collections; vendor-specific protocols such as Logitech HID++ are
// intentionally out of scope for this driver.

#define CFG_WHEEL_MODE    0x55
#define CFG_SCROLL_FACTOR 0x56
#define CFG_WHEEL_DIRECTION 0x57

enum {
    WHEEL_MODE_MOUSE_AXIS = 0,
    WHEEL_MODE_CURSOR_KEYS = 1
};

enum {
    WHEEL_DIRECTION_NORMAL = 0,
    WHEEL_DIRECTION_REVERSED = 1
};

extern "C" int u64_get_usb_hid_config_value(int key, int default_value) __attribute__((weak));

static int usb_hid_get_config_value(int key, int default_value)
{
    if (!u64_get_usb_hid_config_value) {
        return default_value;
    }
    return u64_get_usb_hid_config_value(key, default_value);
}

static int usb_hid_get_scroll_factor(void)
{
    int factor = usb_hid_get_config_value(CFG_SCROLL_FACTOR, 8);
    return HidMouseInterpreter::clampWheelSetting(factor);
}

static int usb_hid_get_wheel_mode(void)
{
    return usb_hid_get_config_value(CFG_WHEEL_MODE, WHEEL_MODE_MOUSE_AXIS);
}

static int usb_hid_get_wheel_direction(void)
{
    return usb_hid_get_config_value(CFG_WHEEL_DIRECTION, WHEEL_DIRECTION_REVERSED);
}

static void usb_hid_set_mouse_button(uint8_t& mouse_joy, uint8_t mask, bool pressed)
{
    if (pressed) {
        mouse_joy &= ~mask;
    } else {
        mouse_joy |= mask;
    }
}

static void usb_hid_queue_key(int key, int repeat)
{
    if (repeat <= 0) {
        return;
    }
    if (repeat > (USB_KEY_BUFFER_SIZE - 1)) {
        repeat = USB_KEY_BUFFER_SIZE - 1;
    }
    system_usb_keyboard.push_head_repeat(key, repeat);
}

static void usb_hid_queue_wheel_keys(int wheel_h, int wheel_v)
{
    if (wheel_v > 0) {
        usb_hid_queue_key(KEY_UP, wheel_v);
    } else if (wheel_v < 0) {
        usb_hid_queue_key(KEY_DOWN, -wheel_v);
    }

    if (wheel_h > 0) {
        usb_hid_queue_key(KEY_RIGHT, wheel_h);
    } else if (wheel_h < 0) {
        usb_hid_queue_key(KEY_LEFT, -wheel_h);
    }
}

// Entry point for call-backs.
void UsbHidDriver_interrupt_callback(void *object) {
	((UsbHidDriver *)object)->interrupt_handler();
}

/*********************************************************************
/ The driver is the "bridge" between the system and the device
/ and necessary system objects
/*********************************************************************/
// tester instance
FactoryRegistrator<UsbInterface *, UsbDriver *> hid_tester(UsbInterface :: getUsbDriverFactory(), UsbHidDriver :: test_driver);

UsbHidDriver :: UsbHidDriver(UsbInterface *intf) : UsbDriver(intf)
{
    device = NULL;
    host = NULL;
    interface = intf;
    irq_in = 0;
    irq_transaction = 0;
    keyboard = false;
    mouse = false;
    descriptor_keyboard = false;
    descriptor_mouse = false;
    mouse_x = mouse_y = 0;
    mouse_joy = 0x1F;
    memset(keyboard_data, 0, sizeof(keyboard_data));
    memset(&rep_button1, 0, sizeof(rep_button1));
    memset(&rep_button2, 0, sizeof(rep_button2));
    memset(&rep_button3, 0, sizeof(rep_button3));
    memset(&rep_mouse_x, 0, sizeof(rep_mouse_x));
    memset(&rep_mouse_y, 0, sizeof(rep_mouse_y));
    memset(&rep_wheel_v, 0, sizeof(rep_wheel_v));
    memset(&rep_wheel_h, 0, sizeof(rep_wheel_h));
    has_button1 = false;
    has_button2 = false;
    has_button3 = false;
    has_wheel_v = false;
    has_wheel_h = false;
    wheel_axis_v_remainder = 0;
    wheel_key_v_remainder = 0;
}

UsbHidDriver :: ~UsbHidDriver()
{
}

UsbDriver * UsbHidDriver :: test_driver(UsbInterface *intf)
{
	if (intf->getInterfaceDescriptor()->interface_class != 3) {
		return NULL;
	}
	printf("** USB HID Device found!\n");
	return new UsbHidDriver(intf);
}

void UsbHidDriver :: install(UsbInterface *intf)
{
	UsbDevice *dev = intf->getParentDevice();
	printf("Installing '%s %s'\n", dev->manufacturer, dev->product);
    host = dev->host;
    interface = intf;
    device = intf->getParentDevice();

	dev->set_configuration(dev->get_device_config()->config_value); // not supposed to be here!

	dev->set_interface(interface->getInterfaceDescriptor()->interface_number,
			interface->getInterfaceDescriptor()->alternate_setting);

	int len = 0;
	uint8_t *reportDescriptor = intf->getHidReportDescriptor(&len);

    uint8_t c_set_idle[]     = { 0x21, 0x0A, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 };
    uint8_t c_set_protocol[] = { 0x21, 0x0B, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 };
    c_set_idle[4] = interface->getInterfaceDescriptor()->interface_number;
    c_set_protocol[4] = interface->getInterfaceDescriptor()->interface_number;

    if (reportDescriptor && (len > 0)) {
        HidReportParser parser;
        if (parser.decode(&report_items, reportDescriptor, len) == 0) {
            t_hid_mouse_fields mouse_fields;
            if (report_items.locateMouseFields(mouse_fields)) {
                descriptor_mouse = true;
                mouse = true;
                rep_mouse_x = mouse_fields.mouse_x;
                rep_mouse_y = mouse_fields.mouse_y;
                rep_button1 = mouse_fields.button1;
                rep_button2 = mouse_fields.button2;
                rep_button3 = mouse_fields.button3;
                rep_wheel_v = mouse_fields.wheel_v;
                rep_wheel_h = mouse_fields.wheel_h;
                has_button1 = mouse_fields.has_button1;
                has_button2 = mouse_fields.has_button2;
                has_button3 = mouse_fields.has_button3;
                has_wheel_v = mouse_fields.has_wheel_v;
                has_wheel_h = mouse_fields.has_wheel_h;
            }

            t_hid_keyboard_fields keyboard_fields;
            if (report_items.locateKeyboardFields(keyboard_fields)) {
                descriptor_keyboard = true;
                keyboard = true;
            }
        }
    }

    bool use_report_protocol = descriptor_mouse || descriptor_keyboard;
    if (!use_report_protocol && (interface->getInterfaceDescriptor()->sub_class == 1)) {
        if (interface->getInterfaceDescriptor()->protocol == 1) {
            printf("Boot Keyboard found!\n");
            keyboard = true;
        } else if (interface->getInterfaceDescriptor()->protocol == 2) {
            printf("Boot Mouse found!\n");
            mouse = true;
        }
    }

    if (!(keyboard || mouse)) {
        return;
    }

	struct t_endpoint_descriptor *iin = interface->find_endpoint(0x83);
    if (!iin) {
        printf("No interrupt IN endpoint found for HID interface %d.\n", interface->getInterfaceDescriptor()->interface_number);
        keyboard = false;
        mouse = false;
        descriptor_keyboard = false;
        descriptor_mouse = false;
        return;
    }

    host->initialize_pipe(&ipipe, device, iin);
	ipipe.Interval = 160; // 50 Hz
    uint16_t max_packet = iin->max_packet_size;
    if ((max_packet == 0) || (max_packet > sizeof(irq_data))) {
        max_packet = sizeof(irq_data);
    }
    ipipe.Length = max_packet;
    ipipe.MaxTrans = max_packet;
	ipipe.buffer = this->irq_data;

	irq_transaction = host->allocate_input_pipe(&ipipe, UsbHidDriver_interrupt_callback, this);
    if (irq_transaction <= 0) {
        printf("Failed to allocate input pipe for HID interface %d.\n", interface->getInterfaceDescriptor()->interface_number);
        keyboard = false;
        mouse = false;
        descriptor_keyboard = false;
        descriptor_mouse = false;
        return;
    }

    if (interface->getInterfaceDescriptor()->sub_class == 1) {
        c_set_protocol[2] = use_report_protocol ? 1 : 0;
        host->control_exchange(&dev->control_pipe, c_set_protocol, 8, NULL, 0);
    }
    host->control_exchange(&dev->control_pipe, c_set_idle, 8, NULL, 0);
#if U64
    C64_MOUSE_EN_1 = mouse ? 1 : 0;
#endif
    host->resume_input_pipe(irq_transaction);
}

void UsbHidDriver :: disable()
{
    if ((host) && (irq_transaction > 0)) {
	host->free_input_pipe(irq_transaction);
        irq_transaction = 0;
    }
    mouse_joy = 0x1F;
#if U64
    C64_MOUSE_EN_1 = 0;
    C64_JOY1_SWOUT = 0x1F;
#endif
}

void UsbHidDriver :: deinstall(UsbInterface *intf)
{
    disable();
    printf("HID deinstalled.\n");
}

void UsbHidDriver :: poll(void)
{
}

void UsbHidDriver :: interrupt_handler()
{
    int data_len = host->getReceivedLength(irq_transaction);
    if (data_len <= 0) {
        host->resume_input_pipe(this->irq_transaction);
        return;
    }
    if (data_len < int(sizeof(irq_data))) {
        memset(irq_data + data_len, 0, sizeof(irq_data) - data_len);
    }

    // printf("I%d: ", interface->getNumber());
    // for(int i=0;i<data_len;i++) {
    //     printf("%b ", irq_data[i]);
    // } printf("\n");

    bool handled = false;

    if (keyboard) {
        bool have_keyboard_report = false;
        if (descriptor_keyboard) {
            have_keyboard_report = report_items.synthesizeBootKeyboardReport(irq_data, data_len, keyboard_data);
        } else {
            HidBootProtocol::copyKeyboardReport(irq_data, data_len, keyboard_data);
            have_keyboard_report = true;
        }
        if (have_keyboard_report) {
            system_usb_keyboard.process_data(keyboard_data);
            handled = true;
        }
    }

    if (mouse) {
        int wheel_v = 0;
        int wheel_h = 0;
        bool handled_mouse = false;

        if (descriptor_mouse) {
            bool axis_report = HidReport::hasValue(irq_data, data_len, rep_mouse_x) || HidReport::hasValue(irq_data, data_len, rep_mouse_y);
            bool button1_report = has_button1 && HidReport::hasValue(irq_data, data_len, rep_button1);
            bool button2_report = has_button2 && HidReport::hasValue(irq_data, data_len, rep_button2);
            bool button3_report = has_button3 && HidReport::hasValue(irq_data, data_len, rep_button3);
            bool wheel_v_report = has_wheel_v && HidReport::hasValue(irq_data, data_len, rep_wheel_v);
            bool wheel_h_report = has_wheel_h && HidReport::hasValue(irq_data, data_len, rep_wheel_h);

            handled_mouse = axis_report || button1_report || button2_report || button3_report || wheel_v_report || wheel_h_report;

            if (button1_report) {
                usb_hid_set_mouse_button(mouse_joy, 0x10, HidReport::getValueFromData(irq_data, data_len, rep_button1) != 0);
            }
            if (button2_report) {
                usb_hid_set_mouse_button(mouse_joy, 0x01, HidReport::getValueFromData(irq_data, data_len, rep_button2) != 0);
            }
            if (button3_report) {
                usb_hid_set_mouse_button(mouse_joy, 0x02, HidReport::getValueFromData(irq_data, data_len, rep_button3) != 0);
            }
            if (axis_report) {
                HidMouseInterpreter::applyRelativeMotion(mouse_x, mouse_y,
                    HidReport::getValueFromData(irq_data, data_len, rep_mouse_x),
                    HidReport::getValueFromData(irq_data, data_len, rep_mouse_y));
            }
            if (wheel_v_report) {
                wheel_v = HidReport::getValueFromData(irq_data, data_len, rep_wheel_v);
            }
            if (wheel_h_report) {
                wheel_h = HidReport::getValueFromData(irq_data, data_len, rep_wheel_h);
            }
        } else {
            t_hid_boot_mouse_sample sample;
            HidBootProtocol::decodeMouseReport(irq_data, data_len, sample);
            handled_mouse = true;
            mouse_joy = 0x1F;
            usb_hid_set_mouse_button(mouse_joy, 0x10, (sample.buttons & 0x01) != 0);
            usb_hid_set_mouse_button(mouse_joy, 0x01, (sample.buttons & 0x02) != 0);
            usb_hid_set_mouse_button(mouse_joy, 0x02, (sample.buttons & 0x04) != 0);
            HidMouseInterpreter::applyRelativeMotion(mouse_x, mouse_y, sample.x, sample.y);
        }

        if (handled_mouse) {
            int scroll_factor = usb_hid_get_scroll_factor();
            if ((wheel_v != 0) || (wheel_h != 0)) {
                if (usb_hid_get_wheel_direction() == WHEEL_DIRECTION_REVERSED) {
                    wheel_h = -wheel_h;
                    wheel_v = -wheel_v;
                }

                if (usb_hid_get_wheel_mode() == WHEEL_MODE_CURSOR_KEYS) {
                    wheel_axis_v_remainder = 0;
                    usb_hid_queue_wheel_keys(
                        HidMouseInterpreter::scaleHorizontalWheelKeys(wheel_h, scroll_factor),
                        HidMouseInterpreter::scaleVerticalWheelKeys(wheel_v, scroll_factor, wheel_key_v_remainder));
                } else {
                    wheel_key_v_remainder = 0;
                    HidMouseInterpreter::applyWheelAxisDeltas(
                        mouse_x,
                        mouse_y,
                        HidMouseInterpreter::scaleHorizontalWheel(wheel_h, scroll_factor),
                        HidMouseInterpreter::scaleVerticalWheel(wheel_v, scroll_factor, wheel_axis_v_remainder));
                }
            }

#if U64
            C64_JOY1_SWOUT = mouse_joy;
            C64_PADDLE_1_X = mouse_x & 0x7F;
            C64_PADDLE_1_Y = mouse_y & 0x7F;
#else
            printf("Mouse: %4x,%4x %b\n", mouse_x, mouse_y, mouse_joy);
#endif
            handled = true;
        }
    }

    if (!handled) {
	    printf("HID (ADDR=%d) IRQ data: ", device->current_address);
	    for(int i=0;i<data_len;i++) {
	        printf("%b ", irq_data[i]);
	    } printf("\n");
	}
    host->resume_input_pipe(this->irq_transaction);
}

void UsbHidDriver :: pipe_error(int pipe) // called from IRQ!
{
	printf("UsbHid ERROR on IRQ pipe.\n");
}

