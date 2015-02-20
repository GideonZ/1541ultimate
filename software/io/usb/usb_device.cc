extern "C" {
	#include "itu.h"
    #include "dump_hex.h"
    #include "small_printf.h"
}
#include "usb_device.h"
#include <stdlib.h>


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
BYTE c_get_device_descriptor[] = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00 };
BYTE c_get_device_descr_slow[] = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x08, 0x00 };
BYTE c_get_string_descriptor[] = { 0x80, 0x06, 0x00, 0x03, 0x00, 0x00, 0x40, 0x00 };
BYTE c_get_configuration[]     = { 0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0x40, 0x00 };
BYTE c_set_address[]           = { 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_set_configuration[]     = { 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
BYTE c_scsi_getmaxlun[]        = { 0xA1, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };

UsbDevice :: UsbDevice(UsbBase *u)
{
    parent = NULL;
    driver = NULL;
    host = u;
    device_state = dev_st_invalid;
    current_address = 0;

    device_descr.length = 0;
    device_config.length = 0;
    interface_descr.length = 0;
    for(int i=0;i<4;i++) {
        endpoints[4].length = 0;
    }
    control_pipe.DevEP = 0;
    control_pipe.MaxTrans = 64; // should depend on speed
}

UsbDevice :: ~UsbDevice()
{
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
               
    printf("Len: %d\n", device_descr.length);
    printf("Type: %d\n", device_descr.type);
    printf("Version: %d\n", device_descr.version);
    printf("Class: %d\n", device_descr.device_class);
    printf("SubClass: %d\n", device_descr.sub_class);
    printf("Protocol: %d\n", device_descr.protocol);
    printf("MaxPacket: %d\n", device_descr.max_packet_size);

	printf("Vendor: %4x\n", le16_to_cpu(device_descr.vendor));
	printf("Product: %4x\n", le16_to_cpu(device_descr.product));
	if(le16_to_cpu(device_descr.vendor) == 0x6e2b) {
//		host->debug = true;
	}
	get_string(device_descr.manuf_string, manufacturer, 32);
	get_string(device_descr.product_string, product, 32);
	get_string(device_descr.serial_string, serial, 32);
	printf("Manufacturer: %s\n", manufacturer);
	printf("Product: %s\n", product);
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
    
    BYTE buf[128];

    int len_descr = host->control_exchange(&control_pipe, c_get_configuration, 8, buf, 128);
    if(len_descr < 0)
    	return false;
    
    int i, len, type, ep;

    for(i = 0;i < len_descr;i++)
        printf("%b ", buf[i]);
    printf("\n");    

    i = 0;
    ep = 0;
    while(i < len_descr) {
        len = (int)buf[i];
        type = (int)buf[i+1];
        switch(type) {
            case DESCR_CONFIGURATION:
                if(len == 9) {
                    memcpy((void *)&device_config, (void *)&buf[i], len);
                } else {
                    printf("Invalid length of configuration descriptor.\n");
                }
                break;
            case DESCR_INTERFACE:                                    
                if(len == 9) {
                    memcpy((void *)&interface_descr, (void *)&buf[i], len);
                } else {
                    printf("Invalid length of interface descriptor.\n");
                }
                break;
            case DESCR_ENDPOINT:                                    
                if(len == 7) {
                    memcpy((void *)&endpoints[ep], (void *)&buf[i], len);
                    // pipe_numbers[ep] = host->create_pipe(current_address, &endpoints[ep]);
                    ep++;
                } else {
                    printf("Invalid length of endpoint descriptor.\n");
                }
                break;
            default:
                printf("Unknown type of descriptor: %d.\n", type);
                i = len_descr; // break out of loop
        }
        i += len;
    }
    printf ("Number of endpoints descriptors found: %d.\n", ep);

    return true;
}

void UsbDevice :: set_configuration(BYTE config)
{
    printf("Setting configuration %d.\n", config);
    c_set_configuration[2] = config;

    BYTE dummy_buffer[8];
    int i = host->control_exchange(&control_pipe, c_set_configuration, 8, dummy_buffer, 0);
    printf("Set Configuration result:%d\n", i);
}

bool UsbDevice :: init(int address)
{
    // first we get the device descriptor
    if(!get_device_descriptor()) // assume full/high speed
    	return false;
    
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

int UsbDevice :: get_max_lun(void)
{
    BYTE dummy_buffer[8];

    int i = host->control_exchange(&control_pipe,
                                   c_scsi_getmaxlun, 8,
                                   dummy_buffer, 8);

    if(!i)
        return 0;

    printf("Got %d bytes. Max lun: %b\n", i, dummy_buffer[0]);
    return (int)dummy_buffer[0];
}


IndexedList<UsbDriver *> usb_drivers(16, NULL);
