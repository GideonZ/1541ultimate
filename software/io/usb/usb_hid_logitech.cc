#include <stdio.h>
#include "integer.h"
#include "usb_hid_logitech.h"
#include "u64.h"

namespace {

static const uint8_t c_dj_mode[] = { 0x20, 0xFF, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t c_notifications[] = { 0x10, 0xFF, 0x80, 0x00, 0x00, 0x09, 0x00 };
static const uint8_t c_query[] = { 0x20, 0xFF, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static uint16_t get_hid_poll_interval(const UsbDevice *device, const struct t_endpoint_descriptor *ep)
{
    uint8_t interval;

    if (!ep) {
        return 160;
    }

    interval = ep->interval ? ep->interval : 10;

    if (device && (device->speed == 2)) {
        if (interval > 16) {
            interval = 16;
        }
        return uint16_t(1U << (interval - 1));
    }

    return uint16_t(interval) * 8;
}

static int send_hid_output_report(UsbDevice *device, UsbInterface *interface, const uint8_t *data, int data_len)
{
    uint8_t c_set_report[] = { 0x21, 0x09, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00 };

    if (!device || !interface || !data || (data_len <= 0)) {
        return -1;
    }

    c_set_report[2] = data[0];
    c_set_report[4] = interface->getInterfaceDescriptor()->interface_number;
    c_set_report[6] = data_len & 0xFF;
    c_set_report[7] = data_len >> 8;

    return device->host->control_write(&device->control_pipe, c_set_report, 8, (void *)data, data_len);
}

static bool is_receiver_descriptor(const uint8_t *reportDescriptor, int len)
{
    bool report10 = false;
    bool report11 = false;
    bool report20 = false;
    bool report21 = false;

    if (!reportDescriptor || (len < 16)) {
        return false;
    }
    if ((reportDescriptor[0] != 0x06) || (reportDescriptor[1] != 0x00) || (reportDescriptor[2] != 0xFF)) {
        return false;
    }
    for (int i = 0; i < (len - 1); i++) {
        if (reportDescriptor[i] != 0x85) {
            continue;
        }
        switch (reportDescriptor[i + 1]) {
        case 0x10:
            report10 = true;
            break;
        case 0x11:
            report11 = true;
            break;
        case 0x20:
            report20 = true;
            break;
        case 0x21:
            report21 = true;
            break;
        }
    }
    return report10 && report11 && report20 && report21;
}

static int sign_extend_12(uint16_t value)
{
    value &= 0x0FFF;
    if (value & 0x0800) {
        return int(value) - 0x1000;
    }
    return int(value);
}

static bool decode_mouse_data(const uint8_t *data, int data_len, uint8_t& buttons, int& xx, int& yy)
{
    const uint8_t *packet = data;
    uint16_t raw_buttons;
    uint16_t raw_x;
    uint16_t raw_y;

    buttons = 0;
    xx = 0;
    yy = 0;

    if (!data || (data_len < 6)) {
        return false;
    }
    if ((data[0] == 0x20) || (data[0] == 0x21)) {
        if (data_len < 10) {
            return false;
        }
        packet = &data[2];
        data_len -= 2;
    }
    if (!packet || (data_len < 6) || (packet[0] != 0x02)) {
        return false;
    }

    raw_buttons = uint16_t(packet[1]) | (uint16_t(packet[2]) << 8);
    if (raw_buttons & 0x0001) buttons |= 0x01;
    if (raw_buttons & 0x0002) buttons |= 0x02;
    if (raw_buttons & 0x0004) buttons |= 0x04;

    raw_x = uint16_t(packet[3]) | ((uint16_t(packet[4]) & 0x0F) << 8);
    raw_y = uint16_t(packet[4] >> 4) | (uint16_t(packet[5]) << 4);

    xx = sign_extend_12(raw_x);
    yy = sign_extend_12(raw_y);
    return true;
}

static uint8_t buttons_to_joy(uint8_t buttons)
{
    uint8_t mouse_joy = 0x1F;

    if (buttons & 0x01) mouse_joy &= ~0x10;
    if (buttons & 0x02) mouse_joy &= ~0x01;
    if (buttons & 0x04) mouse_joy &= ~0x02;
    return mouse_joy;
}

}

void UsbHidLogitechDriver_interrupt_callback(void *object) {
	((UsbHidLogitechDriver *)object)->interrupt_handler();
}

FactoryRegistrator<UsbInterface *, UsbDriver *> hid_logitech_tester(UsbInterface :: getUsbDriverFactory(), UsbHidLogitechDriver :: test_driver);

UsbHidLogitechDriver :: UsbHidLogitechDriver(UsbInterface *intf) : UsbDriver(intf)
{
    device = NULL;
    host = NULL;
    irq_transaction = 0;
    mouse_x = mouse_y = 0;
}

UsbHidLogitechDriver :: ~UsbHidLogitechDriver()
{
}

bool UsbHidLogitechDriver :: test_interface(UsbInterface *intf)
{
    UsbDevice *dev;
    int len = 0;
    uint8_t *reportDescriptor;

    if (!intf) {
        return false;
    }
    if (intf->getInterfaceDescriptor()->interface_class != 3) {
        return false;
    }
    dev = intf->getParentDevice();
    if (!dev || (dev->vendorID != 0x046D)) {
        return false;
    }
    reportDescriptor = intf->getHidReportDescriptor(&len);
    return is_receiver_descriptor(reportDescriptor, len);
}

UsbDriver * UsbHidLogitechDriver :: test_driver(UsbInterface *intf)
{
	if (!test_interface(intf)) {
		return NULL;
	}
	printf("** Logitech HID Receiver found!\n");
	return new UsbHidLogitechDriver(intf);
}

void UsbHidLogitechDriver :: install(UsbInterface *intf)
{
	UsbDevice *dev = intf->getParentDevice();
	uint16_t length;
	printf("Installing Logitech receiver '%s %s'\n", dev->manufacturer, dev->product);
    host = dev->host;
    device = intf->getParentDevice();

	dev->set_configuration(dev->get_device_config()->config_value);
    dev->set_interface(intf->getInterfaceDescriptor()->interface_number,
            intf->getInterfaceDescriptor()->alternate_setting);

    struct t_endpoint_descriptor *iin = intf->find_endpoint(0x83);
	if (!iin) {
		printf("No Logitech HID interrupt endpoint found.\n");
		return;
	}

	host->initialize_pipe(&ipipe, device, iin);
	ipipe.Interval = get_hid_poll_interval(device, iin);
	length = iin->max_packet_size;
	if (!length || (length > sizeof(irq_data))) {
		length = sizeof(irq_data);
	}
	ipipe.Length = length;
	ipipe.MaxTrans = length;
	ipipe.buffer = this->irq_data;

	irq_transaction = host->allocate_input_pipe(&ipipe, UsbHidLogitechDriver_interrupt_callback, this);
	if (irq_transaction <= 0) {
		printf("Failed to allocate Logitech HID input pipe.\n");
		return;
	}

    if (send_hid_output_report(device, intf, c_dj_mode, sizeof(c_dj_mode)) < 0) {
		printf("Failed to switch Logitech receiver to DJ mode.\n");
		host->free_input_pipe(irq_transaction);
		irq_transaction = 0;
		return;
	}
    if (send_hid_output_report(device, intf, c_notifications, sizeof(c_notifications)) < 0) {
		printf("Failed to enable Logitech receiver notifications.\n");
		host->free_input_pipe(irq_transaction);
		irq_transaction = 0;
		return;
	}
    send_hid_output_report(device, intf, c_query, sizeof(c_query));

	printf("Logitech receiver mouse interface found!\n");
#if U64
	C64_MOUSE_EN_1 = 1;
#endif
	host->resume_input_pipe(irq_transaction);
}

void UsbHidLogitechDriver :: disable()
{
	if (irq_transaction > 0) {
		host->free_input_pipe(irq_transaction);
		irq_transaction = 0;
	}
#if U64
	C64_MOUSE_EN_1 = 0;
	C64_JOY1_SWOUT = 0x1F;
#endif
}

void UsbHidLogitechDriver :: deinstall(UsbInterface *intf)
{
	printf("Logitech HID deinstalled.\n");
}

void UsbHidLogitechDriver :: poll(void)
{
}

void UsbHidLogitechDriver :: interrupt_handler()
{
	uint8_t buttons;
	uint8_t mouse_joy;
	int data_len = host->getReceivedLength(irq_transaction);
	int xx;
	int yy;

	if (!decode_mouse_data(irq_data, data_len, buttons, xx, yy)) {
		host->resume_input_pipe(this->irq_transaction);
		return;
	}

	mouse_joy = buttons_to_joy(buttons);
	mouse_x += xx;
	mouse_y -= yy;

#if U64
    C64_MOUSE_EN_1 = 1;
	C64_JOY1_SWOUT = mouse_joy;
	C64_PADDLE_1_X = mouse_x & 0x7F;
	C64_PADDLE_1_Y = mouse_y & 0x7F;
#else
	printf("Mouse: %4x,%4x %b\n", mouse_x, mouse_y, mouse_joy);
#endif

	host->resume_input_pipe(this->irq_transaction);
}

void UsbHidLogitechDriver :: pipe_error(int pipe)
{
	printf("UsbHidLogitech ERROR on IRQ pipe.\n");
}
