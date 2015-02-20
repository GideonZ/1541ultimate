extern "C" {
	#include "itu.h"
    #include "dump_hex.h"
    #include "small_printf.h"
}
#include "usb_base.h"
#include "usb_device.h"

#include <stdlib.h>


UsbBase :: UsbBase()
{
}

UsbBase :: ~UsbBase()
{
}

UsbDevice *UsbBase :: first_device(void)
{
    return device_list[0];
}

void UsbBase :: clean_up()
{
    for(int i=0;i<USB_MAX_DEVICES;i++) {
        if(device_list[i]) {
        	device_list[i]->deinstall();
        	delete device_list[i]; // call destructor of device
        	device_list[i] = 0;
        }
    }
}

int UsbBase :: get_device_slot(void)
{
    for(int i=0;i<USB_MAX_DEVICES;i++) {
        if(!device_list[i])
            return i;
    }
    return -1; // no empty slots
}

    

void UsbBase :: attach_root(void)
{
    printf("Attach root!!\n");
//    max_current = cfg->get_value(CFG_USB_MAXCUR) * 10;
    remaining_current = 500; //max_current;
    bus_reset();

    UsbDevice *dev = new UsbDevice(this);
	install_device(dev, true);
}

bool UsbBase :: install_device(UsbDevice *dev, bool draws_current)
{
    int idx = get_device_slot();
    
    bool ok = false;
    for(int i=0;i<20;i++) { // try 20 times!
        if(dev->init(idx+1)) {
            ok = true;
            break;
        }
    }
    if(!ok)
        return false;

    if(!draws_current) {
        device_list[idx] = dev;
        dev->install();
        return true; // actually install should be able to return false too
    } else {
        int device_curr = int(dev->device_config.max_power) * 2;

        if(device_curr > remaining_current) {
        	printf("Device current (%d mA) exceeds maximum remaining current (%d mA).\n", device_curr, remaining_current);
//            delete dev;
//            return false;
        }
//        else {
        device_list[idx] = dev;
        remaining_current -= device_curr;
        dev->install();
        return true; // actually install should be able to return false too
//        }
    }
    return false;
}

void UsbBase :: deinstall_device(UsbDevice *dev)
{
    dev->deinstall();
    for(int i=0;i<USB_MAX_DEVICES;i++) {
        if(device_list[i] == dev) {
        	device_list[i] = NULL;
        	delete dev;
        	break;
        }
    }
}

