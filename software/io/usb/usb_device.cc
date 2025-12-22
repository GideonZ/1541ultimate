extern "C" {
	#include "itu.h"
    #include "dump_hex.h"
    #include "small_printf.h"
}
#include "usb_device.h"
#include <stdlib.h>
#include <string.h>
#include "task.h"
#include "endianness.h"

#if DISABLE_USB_DEBUG
    #define printf(...)
#endif

char *unicode_to_ascii(uint8_t *in, char *out, int maxlen)
{
    char *buf;
    uint8_t len = (*in);
    
    buf = out;
    in += 2;

    if(len < 2) {
    	out[0] = 0;
    } else {
		len -= 2;
		len >>= 1;
		if(len >= maxlen)
			len = maxlen-1;

		while(len) {
			if ((*in >= 32) && (*in < 127))
				*(buf++) = (char)*in;
			in += 2;
			len--;
		}
		*(buf++) = '\0';
    }
    return out;
}


// =========================================================
// USB DEVICE
// =========================================================
uint8_t c_get_device_descriptor[]     = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00 };
uint8_t c_get_device_descr_slow[]     = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x08, 0x00 };
uint8_t c_get_language_descriptor[]   = { 0x80, 0x06, 0x00, 0x03, 0x00, 0x00, 0x04, 0x00 };
uint8_t c_get_configuration[]         = { 0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0x80, 0x00 };
uint8_t c_set_address[]               = { 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_set_configuration[]         = { 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_set_interface[]             = { 0x01, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

uint8_t c_get_interface[]			   = { 0x21, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };
uint8_t c_get_hid_report_descriptor[]  = { 0x81, 0x06, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00 };
uint8_t c_unstall_pipe[]			   = { 0x02, 0x01, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 };

UsbDevice :: UsbDevice(UsbBase *u, int speed)
{
	disabled = false;
	parent = NULL;
    parent_port = 0;
    host = u;
    device_state = dev_st_invalid;
    current_address = 0;
    config_descriptor = NULL;

    device_descr.length = 0;

    for(int i=0;i<4;i++) {
    	interfaces[i] = NULL;
    }
    num_interfaces = 0;
    control_pipe.DevEP = 0;
    control_pipe.device = this;
    strcpy(control_pipe.name, "Root|0");

    this->speed = speed;
    if (speed == 0) {
    	control_pipe.MaxTrans = 8;
    } else {
    	control_pipe.MaxTrans = 64;
    }
	control_pipe.SplitCtl = 0;
	control_pipe.highSpeed = (speed == 2) ? 1 : 0;
	control_pipe.needPing = 0;

	manufacturer[0] = 0;
	product[0] = 0;
	vendorID = 0;
	productID = 0;
}

UsbDevice :: ~UsbDevice()
{
	for (int i=0;i<num_interfaces;i++) {
		if (interfaces[i]) {
			delete interfaces[i];
		}
	}
	if(config_descriptor) {
		delete config_descriptor;
	}
}


void UsbDevice :: disable()
{
	disabled = true;
	for(int i=0;i<num_interfaces;i++) {
		if (interfaces[i]) {
			interfaces[i] -> disable();
		}
	}
}

void UsbDevice :: get_string(int index, char *dest, int len)
{
    uint8_t usb_buffer[256];
    
    uint8_t c_get_string_descriptor[] = { 0x80, 0x06, 0x00, 0x03, 0x09, 0x04, 0xFF, 0x00 };

    c_get_string_descriptor[2] = (uint8_t)index;
    c_get_string_descriptor[4] = language[0];
    c_get_string_descriptor[5] = language[1];

    int result = host->control_exchange(&control_pipe, c_get_string_descriptor, 8, usb_buffer, 256);
    if(result > 0) {
    	unicode_to_ascii(usb_buffer, dest, len);
        //printf("USB Get String returned:\n");
        //dump_hex_relative(usb_buffer, result);
    } else {
        //printf("USB Get String returned nothing.");
        *dest = '\0';
    }
}

void UsbDevice :: get_language(void)
{
    uint8_t usb_buffer[4];

	language[0] = 0;
	language[1] = 0;
    int result = host->control_exchange(&control_pipe, c_get_language_descriptor, 8, usb_buffer, 4);
    if(result > 0) {
    	language[0] = usb_buffer[2];
    	language[1] = usb_buffer[3];
    }
}

bool UsbDevice :: get_device_descriptor()
{
    int i;

    i = host->control_exchange(&control_pipe, c_get_device_descriptor, 8, &device_descr, 18);
	if(i != 18) {
		printf("Error: Expected 18 bytes on device descriptor.. got %d.\n", i);
		if (i >= 8) {
			dump_hex_relative(&device_descr, i);
			control_pipe.MaxTrans = device_descr.max_packet_size;
			printf("Switched to 8 byte control pipe.\n");
		}
		return false;
	} else {
	    switch(device_descr.max_packet_size) {
	    case 8:
        case 16:
        case 32:
        case 64:
	        control_pipe.MaxTrans = device_descr.max_packet_size;
	        printf("This device has a valid maxPacketSize of %d for the control pipe.\n", device_descr.max_packet_size);
	        break;
        default:
            printf("Erroneous maxPacketSize for control pipe:\n");
            dump_hex_relative(&device_descr, 18);
            break;
	    }
    }

	vendorID = le16_to_cpu(device_descr.vendor);
	productID = le16_to_cpu(device_descr.product);
	printf("Vendor/Product: %4x %4x\n", vendorID, productID);
/*
    printf("Class / SubClass / Protocol: %d %d %d\n", device_descr.device_class, device_descr.sub_class, device_descr.protocol);
    printf("MaxPacket: %d\n", device_descr.max_packet_size);
	printf("Configurations: %d\n", device_descr.num_configurations);
*/
    return true;
}

bool UsbDevice :: set_address(int address)
{
    printf ("Setting address to %d: \n", address);

    uint8_t dummy_buffer[20];

    c_set_address[2] = (uint8_t)address;
    int i = host->control_exchange(&control_pipe, c_set_address, 8, dummy_buffer, 0);
    //printf("Got %d bytes back.\n", i);
    
    if(i >= 0) {
        current_address = address;
        control_pipe.DevEP = (address << 8);
        return true;
    }
    return false;
}

bool UsbDevice :: get_configuration(uint8_t index)
{
    printf ("Get configuration %d: \n", index);
    c_get_configuration[2] = index;
    c_get_configuration[6] = 9;
    c_get_configuration[7] = 0;
    uint8_t buf[16];
    memset(buf, 0xAA, 16);

    config_descriptor = NULL;
    int len_descr = host->control_exchange(&control_pipe, c_get_configuration, 8, buf, 9);
    //dump_hex(buf, 16);

    if(len_descr < 0)
    	return false;
    
    if ((buf[0] == 9) && (buf[1] == DESCR_CONFIGURATION)) {
        len_descr = int(buf[2]) + 256*int(buf[3]);
        // printf("Total configuration length: %d\n", len_descr);
    	config_descriptor = new uint8_t[len_descr + 8];
    	if (!config_descriptor)
    		return false;
    } else {
    	printf("Invalid configuration descriptor. Received: %d bytes.\n", len_descr);
    	dump_hex(buf, len_descr);
    	return false;
    }

    // copy length
    c_get_configuration[6] = buf[2];
    c_get_configuration[7] = buf[3];
    int read = host->control_exchange(&control_pipe, c_get_configuration, 8, config_descriptor, len_descr);

    if (read != len_descr) {
    	printf("Error reading complete config descriptor. Got %d bytes out of %d.\n", read, len_descr);
    	return false;
    }

    //dump_hex(config_descriptor, len_descr);

    uint8_t *pnt = config_descriptor;
    int i, len, type, ep;

    i = 0;
    ep = 0;
    num_interfaces = 0;
    uint8_t bConfigurationValue = config_descriptor[5];
    printf("bConfigurationValue = %b\n", bConfigurationValue);

    UsbInterface *interface = NULL;

    while(i < len_descr) {
        len = (int)pnt[0];
        if (!len) {
        	printf("Invalid descriptor length 0. exit\n");
        	break;
        }
        type = (int)pnt[1];
        //dump_hex(pnt, len);
        switch(type) {
        	case DESCR_CONFIGURATION:
        		break;
        	case DESCR_INTERFACE:
        		if(len == 9) {
        			printf("Interface descriptor #%b:%b, with %d endpoints. Class = %d:%d:%d\n", pnt[2], pnt[3], pnt[4], pnt[5], pnt[6], pnt[7]);
        			int number = (int)pnt[2];
        			if (number <= 3) {
						interface = new UsbInterface(this, number, (struct t_interface_descriptor *)pnt);
						if (interfaces[number]) {
							interfaces[number]->addAlternative(interface);
						} else {
							interfaces[number] = interface;
							num_interfaces++;
						}
        			}
        		}
        		break;
        	case DESCR_ENDPOINT:
                if ((len == 7)||(len == 9)) {
                	if (!interface) {
                		printf("Cannot store endpoint, as we do not have an active interface to attach it to.\n");
                	} else {
                		interface->addEndpoint(pnt, len);
                	}
                } else {
                    printf("Invalid length of endpoint descriptor.\n");
                }
                break;
            case DESCR_HID:
            	if(len == 9) {
            		int hid_len = int(pnt[7]) + 256*int(pnt[8]);
            		printf("Interface has a HID descriptor with length %d!\n", hid_len);
            		interface->setHidDescriptor(pnt, len);
           		} else {
            		printf("Invalid length of HID descriptor.\n");
            	}
            	break;
            case DESCR_CS_INTERFACE:
            	if ((len >= 9) && (pnt[2] == 1)) {
            		//printf("Device has a class specific interface descriptor #%d with length %d!\n", pnt[8], int(pnt[5]) + 256*int(pnt[6]));
            	}
            	break;
            case DESCR_CS_ENDPOINT:
				//printf("Device has an class specific endpoint descriptor\n");
            	break;

            default:
                printf("Unknown type of descriptor: %d.\n", type);
                //i = len_descr; // break out of loop
                break;
        }
        i += len;
        pnt += len;
    }
    printf ("Number of interfaces found: %d.\n", num_interfaces);

    return true;
}

struct t_device_configuration *UsbDevice :: get_device_config()
{
	return (struct t_device_configuration *)config_descriptor;
}

void UsbDevice :: set_configuration(uint8_t config)
{
    printf("Setting configuration %d.\n", config);
    c_set_configuration[2] = config;

    uint8_t dummy_buffer[8];
    int i = host->control_exchange(&control_pipe, c_set_configuration, 8, dummy_buffer, 0);
//    printf("Set Configuration result:%d\n", i);
}

void UsbDevice :: set_interface(uint8_t interface, uint8_t alt)
{
    printf("Setting interface %d to alternate setting %d.\n", interface, alt);
    c_set_interface[2] = alt;
    c_set_interface[4] = interface;

    uint8_t dummy_buffer[8];
    int i = host->control_exchange(&control_pipe, c_set_interface, 8, dummy_buffer, 0);
    printf("Set Configuration result:%d\n", i);
//    interface_number = alt;
}

int UsbDevice :: unstall_pipe(uint8_t ep)
{
	c_unstall_pipe[4] = ep;
	return host->control_exchange(&control_pipe, c_unstall_pipe, 8, NULL, 0);
}

bool UsbDevice :: init(int address)
{
    // first we get the device descriptor
    if(!get_device_descriptor()) { // assume full/high speed
    	device_reset();
    	return false;
    }

    // set address and create new control pipes for this address
    return set_address(address);
}

bool UsbDevice :: init2(void)
{
    get_language();
    get_string(device_descr.manuf_string, manufacturer, 32);
	get_string(device_descr.serial_string, serial, 32);
	get_string(device_descr.product_string, product, 32);

	if (*manufacturer)
		printf("Manufacturer: %s\n", manufacturer);
	if (*product)
		printf("Product: %s\n", product);
	if (*serial)
		printf("Serial #: %s\n", serial);

    // get configuration
    return get_configuration(0);
}

char *UsbDevice :: get_pathname(char *dest, int len)
{
	char buf[16];
	memset(buf, 0, 16);
	UsbDevice *d = this;
	int i=15;
	while(d->parent != NULL) {
		buf[--i] = 0x30 + d->parent_port;
		//buf[--i] = 0x2E;
		d = d->parent;
	}
	strncpy(dest, "USB", 3);
	strncpy(dest+3, (const char *)&buf[i], len-3);
	return dest;
}


// =========================================================
// USB INTERFACE
// =========================================================

uint8_t *UsbInterface :: getHidReportDescriptor(int *len) {
	if (!hid_descriptor_valid) {
		return NULL;
	}
    uint8_t *pnt = (uint8_t *)&hid_descriptor;
    int hid_len = int(pnt[7]) + 256*int(pnt[8]);
    *len = hid_len;

	if (hid_report_descriptor) {
	    return hid_report_descriptor;
	}

	uint8_t buf[2];
	hid_report_descriptor = new uint8_t[hid_len];

	if (hid_report_descriptor) {
		c_get_interface[4] = interface_number;
		parentDevice->host->control_exchange(&parentDevice->control_pipe, c_get_interface, 8, buf, 1);

		c_get_hid_report_descriptor[4] = interface_number;
		c_get_hid_report_descriptor[6] = pnt[7];
		c_get_hid_report_descriptor[7] = pnt[8];
		hid_len = parentDevice->host->control_exchange(&parentDevice->control_pipe, c_get_hid_report_descriptor, 8, hid_report_descriptor, hid_len);
		// dump_hex(hid_report_descriptor, hid_len);
		return hid_report_descriptor;
	}
	return NULL; // insufficient mem.. FATAL
}


IndexedList<UsbDriver *> usb_drivers(16, NULL);
