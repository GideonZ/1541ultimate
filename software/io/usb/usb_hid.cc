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

// Entry point for call-backs.
void UsbHidDriver_interrupt_callback(uint8_t *data, int data_length, void *object) {
	((UsbHidDriver *)object)->interrupt_handler(data, data_length);
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
}

UsbHidDriver :: ~UsbHidDriver()
{
}

UsbDriver * UsbHidDriver :: test_driver(UsbInterface *intf)
{
	if (intf->getInterfaceDescriptor()->interface_class != 3) {
		return NULL;
	}
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

    struct t_endpoint_descriptor *iin = interface->find_endpoint(0x83);
    irq_in   = (iin->endpoint_address & 0x0F);

	struct t_pipe ipipe;
	ipipe.DevEP = uint16_t((device->current_address << 8) | irq_in);
	ipipe.Interval = 160; // 50 Hz
	ipipe.Length = 8; // just read 8 bytes max
	ipipe.MaxTrans = 16; // iin->max_packet_size;
	ipipe.SplitCtl = host->getSplitControl(device->parent->current_address, device->parent_port + 1, device->speed, 3);
	ipipe.Command = 0; // driver will fill in the command
	ipipe.highSpeed = 0;
	irq_transaction = host->allocate_input_pipe(&ipipe, UsbHidDriver_interrupt_callback, this);

	if (interface->getInterfaceDescriptor()->sub_class == 1) {
		if (interface->getInterfaceDescriptor()->protocol == 1) {
			printf("Boot Keyboard found!\n");
			keyboard = true;
		} else if (interface->getInterfaceDescriptor()->protocol == 2) {
			printf("Boot Mouse found!\n");
		}
	}
}

void UsbHidDriver :: disable()
{
	host->free_input_pipe(irq_transaction);
}

void UsbHidDriver :: deinstall(UsbInterface *intf)
{
    printf("HID deinstalled.\n");
}

void UsbHidDriver :: poll(void)
{
}

void UsbHidDriver :: interrupt_handler(uint8_t *irq_data, int data_len)
{
    printf("HID (ADDR=%d) IRQ data: ", device->current_address);
	for(int i=0;i<data_len;i++) {
		printf("%b ", irq_data[i]);
	} printf("\n");

	if (keyboard) { // keyboard
		system_usb_keyboard.process_data(irq_data);
	}
	host->resume_input_pipe(this->irq_transaction);
}

void UsbHidDriver :: pipe_error(int pipe) // called from IRQ!
{
	printf("UsbHid ERROR on IRQ pipe.\n");
}

