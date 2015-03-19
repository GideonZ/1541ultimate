extern "C" {
	#include "itu.h"
    #include "dump_hex.h"
    #include "small_printf.h"
}
#include "usb_device.h"
#include <stdlib.h>
#include <string.h>

__inline WORD le16_to_cpu(WORD h)
{
    return (h >> 8) | (h << 8);
}

char *unicode_to_ascii(BYTE *in, char *out, int maxlen)
{
    char *buf;
    BYTE len = (*in);
    
    buf = out;
    in += 2;

    if(len < 2)
        return "";

    len -= 2;
    len >>= 1;
    if(len >= maxlen)
        len = maxlen-1;
        
    while(len) {
        *(buf++) = (char)*in;
        in += 2;
        len--;
    }
    *(buf++) = '\0';

    return out;
}

// =========================================================
// USB DEVICE
// =========================================================
BYTE c_get_device_descriptor[]     = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00 };
BYTE c_get_device_descr_slow[]     = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x08, 0x00 };
BYTE c_get_string_descriptor[]     = { 0x80, 0x06, 0x00, 0x03, 0x00, 0x00, 0x40, 0x00 };
BYTE c_get_configuration[]         = { 0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0x80, 0x00 };
BYTE c_set_address[]               = { 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_set_configuration[]         = { 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

BYTE c_get_interface[]			   = { 0x21, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };
BYTE c_get_hid_report_descriptor[] = { 0x81, 0x06, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00 };

UsbDevice :: UsbDevice(UsbBase *u, int speed)
{
    parent = NULL;
    parent_port = 0;
    driver = NULL;
    host = u;
    device_state = dev_st_invalid;
    current_address = 0;
    config_descriptor = NULL;
    hid_descriptor = NULL;

    device_descr.length = 0;
    for(int i=0;i<4;i++) {
        endpoints[i].length = 0;
    }
    for(int i=0;i<4;i++) {
    	interfaces[i] = NULL;
    }
    num_interfaces = 0;
    control_pipe.DevEP = 0;
    this->speed = speed;
    if (speed == 0) {
    	control_pipe.MaxTrans = 8;
    } else {
    	control_pipe.MaxTrans = 64;
    }
	control_pipe.SplitCtl = 0;
}

UsbDevice :: ~UsbDevice()
{
	if(config_descriptor)
		delete config_descriptor;
	if(hid_descriptor)
		delete hid_descriptor;
}


void UsbDevice :: get_string(int index, char *dest, int len)
{
    if(!index) {
        *dest = '\0';
        return;
    }
        
    BYTE usb_buffer[64];
    
    c_get_string_descriptor[2] = (BYTE)index;

    if(host->control_exchange(&control_pipe, c_get_string_descriptor, 8, usb_buffer, 64) > 0)
        unicode_to_ascii(usb_buffer, dest, len);
    else
        *dest = '\0';
      
}

bool UsbDevice :: get_device_descriptor()
{
    int i;

	i = host->control_exchange(&control_pipe, c_get_device_descriptor, 8, &device_descr, 18);
	if(i != 18) {
		printf("Error: Expected 18 bytes on device descriptor.. got %d.\n", i);
		return false;
	}

	printf("Vendor/Product: %4x %4x\n", le16_to_cpu(device_descr.vendor), le16_to_cpu(device_descr.product));
    printf("Class / SubClass / Protocol: %d %d %d\n", device_descr.device_class, device_descr.sub_class, device_descr.protocol);
    printf("MaxPacket: %d\n", device_descr.max_packet_size);

	get_string(device_descr.manuf_string, manufacturer, 32);
	get_string(device_descr.product_string, product, 32);
	get_string(device_descr.serial_string, serial, 32);
	if (*manufacturer)
		printf("Manufacturer: %s\n", manufacturer);
	if (*product)
		printf("Product: %s\n", product);
	if (*serial)
		printf("Serial #: %s\n", serial);
	printf("Configurations: %d\n", device_descr.num_configurations);
    return true;
}

void UsbDevice :: set_address(int address)
{
    printf ("Setting address to %d: \n", address);

    BYTE dummy_buffer[8];

    c_set_address[2] = (BYTE)address;
    int i = host->control_exchange(&control_pipe, c_set_address, 8, dummy_buffer, 0);
    //printf("Got %d bytes back.\n", i);
    
    if(i >= 0) {
        current_address = address;
        control_pipe.DevEP = (address << 8);
    }
}

bool UsbDevice :: get_configuration(BYTE index)
{
    printf ("Get configuration %d: \n", index);
    c_get_configuration[2] = index;
    c_get_configuration[6] = 9;
    c_get_configuration[7] = 0;
    BYTE buf[12];

    config_descriptor = NULL;
    int len_descr = host->control_exchange(&control_pipe, c_get_configuration, 8, buf, 9);
    if(len_descr < 0)
    	return false;
    
    if ((buf[0] == 9) && (buf[1] == DESCR_CONFIGURATION)) {
        len_descr = int(buf[2]) + 256*int(buf[3]);
        printf("Total configuration length: %d\n", len_descr);
    	config_descriptor = new BYTE[len_descr];
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

    BYTE *pnt = config_descriptor;
    int i, len, type, ep;

    i = 0;
    ep = 0;
    num_interfaces = 0;

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
        			printf("Interface descriptor #%d, with %d endpoints. Class = %d:%d\n", pnt[2], pnt[4], pnt[5], pnt[6]);
        			if (pnt[4] != 0) {
        				interfaces[num_interfaces] = (struct t_interface_descriptor *)pnt;
        				num_interfaces++;
        			}
        		}
        		break;
        	case DESCR_ENDPOINT:
                if ((len == 7)||(len == 9)) {
                	printf("Endpoint found with address %b, attr: %b\n", pnt[2], pnt[3]);
                	if (ep < 4) {
                		memcpy((void *)&endpoints[ep], (void *)pnt, len);
                	}
                    // pipe_numbers[ep] = host->create_pipe(current_address, &endpoints[ep]);
                    ep++;
                } else {
                    printf("Invalid length of endpoint descriptor.\n");
                }
                break;
            case DESCR_HID:
            	if(len == 9) {
            		int hid_len = int(pnt[7]) + 256*int(pnt[8]);
            		printf("Device has a HID descriptor with length %d!\n", hid_len);

            		/* THIS MAY ONLY BE DONE AFTER SET_CONFIGURATION.
            		if (!hid_descriptor) {
            			hid_descriptor = new BYTE[hid_len];
            			if (hid_descriptor) {
            				BYTE current_interface = interfaces[num_interfaces-1]->interface_number;
            				printf("Current interface number = %b\n", current_interface);

            				c_get_interface[4] = current_interface;
            				host->control_exchange(&control_pipe, c_get_interface, 8, buf, 1);

            				c_get_hid_report_descriptor[4] = current_interface;
            				c_get_hid_report_descriptor[6] = pnt[7];
            				c_get_hid_report_descriptor[7] = pnt[8];
            				hid_len = host->control_exchange(&control_pipe, c_get_hid_report_descriptor, 8, hid_descriptor, hid_len);
            				dump_hex(hid_descriptor, hid_len);
            			}
            		}
            		*/
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
    printf ("Number of endpoints found: %d.\n", ep);

    return true;
}

struct t_device_configuration *UsbDevice :: get_device_config()
{
	return (struct t_device_configuration *)config_descriptor;
}

void UsbDevice :: set_configuration(BYTE config)
{
//    printf("Setting configuration %d.\n", config);
    c_set_configuration[2] = config;

    BYTE dummy_buffer[8];
    int i = host->control_exchange(&control_pipe, c_set_configuration, 8, dummy_buffer, 0);
//    printf("Set Configuration result:%d\n", i);
}

bool UsbDevice :: init(int address)
{
    // first we get the device descriptor
    if(!get_device_descriptor()) { // assume full/high speed
    	device_reset();
    	return false;
    }
    
    // set address and create new control pipes for this address
    set_address(address);

    // get configuration
    return get_configuration(0);
}

int UsbDevice :: find_endpoint(BYTE code)
{
    BYTE ep_code;
    
    for(int i=0;i<4;i++) {
        if(endpoints[i].length) {
            ep_code = (endpoints[i].attributes & 0x03) |
                      (endpoints[i].endpoint_address & 0x80);
            if (ep_code == code)
                return endpoints[i].endpoint_address;
        }
    }
    return -1;
}

void UsbDevice :: get_pathname(char *dest, int len)
{
	char buf[16];
	memset(buf, 0, 16);
	UsbDevice *d = this;
	int i=15;
	while(d->parent != NULL) {
		buf[--i] = 0x30 + d->parent_port;
		buf[--i] = 0x2E;
		d = d->parent;
	}
	strncpy(dest, "Usb", 3);
	strncpy(dest+3, (const char *)&buf[i], len-3);
}

IndexedList<UsbDriver *> usb_drivers(16, NULL);
