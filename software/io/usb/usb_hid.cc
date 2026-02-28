#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "itu.h"
#include "dump_hex.h"
#include "integer.h"
#include "usb_hid.h"
#include "profiler.h"
#include "FreeRTOS.h"
#include "task.h"
#include "keyboard_usb.h"
#include "u64.h"

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
    mouse_x = mouse_y = 0;
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
	if (reportDescriptor) {
	    dump_hex_relative(reportDescriptor, len);
	}


	struct t_endpoint_descriptor *iin = interface->find_endpoint(0x83);

    host->initialize_pipe(&ipipe, device, iin);
	ipipe.Interval = 160; // 50 Hz
	ipipe.Length = 16; // just read 8 bytes max
	ipipe.MaxTrans = 16; // iin->max_packet_size;
	ipipe.buffer = this->irq_data;

	irq_transaction = host->allocate_input_pipe(&ipipe, UsbHidDriver_interrupt_callback, this);

    uint8_t c_set_idle[]     = { 0x21, 0x0A, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 };
    uint8_t c_set_protocol[] = { 0x21, 0x0B, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 };
    c_set_idle[4] = interface->getInterfaceDescriptor()->interface_number;
    c_set_protocol[4] = interface->getInterfaceDescriptor()->interface_number;

    if (interface->getInterfaceDescriptor()->sub_class == 1) {
		if (interface->getInterfaceDescriptor()->protocol == 1) {
			printf("Boot Keyboard found!\n");
			keyboard = true;

			host->control_exchange(&dev->control_pipe, c_set_protocol, 8, NULL, 0);
			host->control_exchange(&dev->control_pipe, c_set_idle, 8, NULL, 0);
			host->resume_input_pipe(irq_transaction); // start polling
		} else if (interface->getInterfaceDescriptor()->protocol == 2) {
			printf("Boot Mouse found (maxtrans = %d)!\n", iin->max_packet_size);
            mouse = true;
#if U64
            C64_MOUSE_EN_1 = 1;
#endif
            host->resume_input_pipe(irq_transaction); // start polling
			// Note: Polling is not started!
		}
    } else {
            // host->resume_input_pipe(irq_transaction); // start polling
			// Another HID device which we cannot decode just yet
			// Note: Polling is not started!
	}
}

void UsbHidDriver :: disable()
{
	host->free_input_pipe(irq_transaction);
#if U64
    C64_MOUSE_EN_1 = 0;
    C64_JOY1_SWOUT = 0x1F;
#endif
}

void UsbHidDriver :: deinstall(UsbInterface *intf)
{
    printf("HID deinstalled.\n");
}

void UsbHidDriver :: poll(void)
{
}

void UsbHidDriver :: interrupt_handler()
{
    int data_len = host->getReceivedLength(irq_transaction);

    if (keyboard) { // keyboard
		system_usb_keyboard.process_data(irq_data);
	} else if (mouse) { // mouse
        mouse_x += irq_data[1];
        mouse_y -= irq_data[2];
        
        uint8_t mouse_joy = 0x1F;
        if (irq_data[0] & 0x01) mouse_joy &= ~0x10;
        if (irq_data[0] & 0x02) mouse_joy &= ~0x01;
        if (irq_data[0] & 0x04) mouse_joy &= ~0x02;
#if U64
        C64_JOY1_SWOUT = mouse_joy;
        C64_PADDLE_1_X = mouse_x & 0x7F;
        C64_PADDLE_1_Y = mouse_y & 0x7F;
#else
        printf("Mouse: %4x,%4x %b\n", mouse_x, mouse_y, mouse_joy);
#endif

    } else {
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

