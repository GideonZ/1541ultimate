/*
 * usb_test.cc
 *
 *  Created on: Nov 16, 2016
 *      Author: gideon
 */

#include "usb_base.h"
#include "usb_hub.h"

extern "C" {
void initializeUsb(void) {
	usb2.initHardware();
}

int checkUsbHub(void)
{
	UsbDevice *device = usb2.first_device();
	if (!device) {
//		printf("No USB device was found on the USB PHY.\n");
		return -1;
	}
	if (device->vendorID != 0x0424) {
		printf("Primary device is not from Microchip %04x.\n", device->vendorID);
		return -2;
	}
	if (device->productID != 0x2512) {
		printf("Primary device does not have the right product ID.\n", device->productID);
		return -3;
	}
	return 0;
}

int checkUsbSticks(void)
{
	UsbDevice *device = usb2.first_device();
	if (!device) {
		printf("No USB device was found on the USB PHY.\n");
		return -1;
	}
	UsbInterface *interface = device->interfaces[0];
	if (!interface) {
		printf("Primary device does not seem to be initialized correctly.\n");
		return -3;
	}
	UsbHubDriver *hub = (UsbHubDriver *)interface->getDriver();
	if (!hub) {
		printf("No HUB driver?\n");
		return -4;
	}
	int devices = 0;
	for(int i=0;i<3;i++) {
		UsbDevice *d = hub->getChild(i);
		if (d) {
			if (d->speed == 2) {
				devices++;
			}
		}
	}
	if (devices != 3) {
		return -2;
	}
	return 0;
}

void printUsbSticksFound(void)
{
	UsbDevice *device = usb2.first_device();
	if (!device) {
		return;
	}
	UsbInterface *interface = device->interfaces[0];
	if (!interface) {
		return;
	}
	UsbHubDriver *hub = (UsbHubDriver *)interface->getDriver();
	if (!hub) {
		return;
	}
	for(int i=0;i<3;i++) {
		UsbDevice *d = hub->getChild(i);
		if (d) {
			printf("Port %d: %04x:%04x %s %s\n", i, d->vendorID, d->productID, d->manufacturer, d->product);
		}
	}
}
}
