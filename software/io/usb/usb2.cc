extern "C" {
	#include "itu.h"
    #include "dump_hex.h"
    #include "small_printf.h"
}
#include "usb2.h"
#include <stdlib.h>

extern BYTE  _binary_nano_usb_b_start;
extern DWORD _binary_nano_usb_b_size;

Usb2 usb2; // the global

__inline WORD le16_to_cpu(WORD h)
{
    return (h >> 8) | (h << 8);
}


static void poll_usb2(Event &e)
{
	usb2.poll(e);
}

Usb2 :: Usb2()
{
    initialized = false;
    
    if(CAPABILITIES & CAPAB_USB_HOST2) {
	    init();

#ifndef BOOTLOADER
        poll_list.append(&poll_usb2);
#endif
    }
}

Usb2 :: ~Usb2()
{
#ifndef BOOTLOADER
	poll_list.remove(&poll_usb2);
#endif
	clean_up();
	initialized = false;
}

void Usb2 :: init(void)
{
    // clear our internal device list
    for(int i=0;i<USB_MAX_DEVICES;i++) {
        device_list[i] = NULL;
    }
    device_present = false;
    for(int i=0;i<MAX_INPUT_PIPES;i++) {
    	inputPipeControlWords[i] = 0;
    }

    // load the nano CPU code and start it.
    int size = (int)&_binary_nano_usb_b_size;
    BYTE *src = &_binary_nano_usb_b_start;
    BYTE *dst = (BYTE *)HW_USB2_CODE_BE;
    for(int i=0;i<size;i++)
        *(dst++) = *(src++);
    printf("Nano CPU based USB Controller: %d bytes loaded.\n", size);

    USB2_NANO_ENABLE = 1;

    initialized = true;
}

void Usb2 :: clean_up()
{
    // destruct all devices (possibly shutting down the attached devices) 
    for(int i=0;i<USB_MAX_DEVICES;i++) {
        if(device_list[i]) {
        	device_list[i]->deinstall();
        	delete device_list[i]; // call destructor of device
            device_list[i] = NULL;
        }
    }
    // stop Nano CPU by putting it in reset
    USB2_NANO_ENABLE = 0;
}


void Usb2 :: attach_root(void)
{
    printf("Attach root!!\n");

    UsbDevice *dev = new UsbDevice(this);
	install_device(dev, true);
}

bool Usb2 :: install_device(UsbDevice *dev, bool draws_current)
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

    int device_curr = int(dev->device_config.max_power) * 2;
    printf("Device needs %d mA\n", device_curr);
    device_list[idx] = dev;
    dev->install();
    return true; // actually install should be able to return false too
}

void Usb2 :: deinstall_device(UsbDevice *dev)
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
    
void Usb2 :: poll(Event &e)
{
	if(!initialized) {
		init();
        return;
    }
    DWORD fifo_data;
    static BYTE buffertje[64];
    if (get_fifo(&fifo_data)) {
        printf("USB2 attr fifo: %8x\n", fifo_data);
        if (fifo_data == USB2_EVENT_ATTACH ) { // connect!
            get_fifo(&fifo_data);
            if (fifo_data == 2) {
                printf("High speed device connected\n");
            } else if(fifo_data == 1) {
                printf("Full speed device connected\n");
            } else if(fifo_data == 0) {
                printf("Low speed device connected\n");
            } else {
                printf("Error connecting device\n");
            }
            attach_root();
        }
    }
}

bool Usb2 :: get_fifo(DWORD *out)
{
    BYTE tail = USB2_ATTR_FIFO_TAIL;
    BYTE head = USB2_ATTR_FIFO_HEAD;
    BYTE count = head - tail;
    DWORD d = 0;
    if (count) {
        int offset = ((int)tail) << 1;
        d = USB2_ATTR_FIFO_DATA8(offset);
        d |= ((DWORD)USB2_ATTR_FIFO_DATA8(offset+1)) << 8;
        USB2_ATTR_FIFO_TAIL = (tail + 1) & USB2_ATTR_FIFO_MASK;
        *out = d;
        return true;        
    }
    return false;
}

/*
#define USB2_CMD_CMD      *((volatile BYTE *)(USB2_CODE_BASE + 0x7E0))
#define USB2_CMD_DEV_ADDR *((volatile WORD *)(USB2_CODE_BASE + 0x7E2))
#define USB2_CMD_MEM_ADDR *((volatile DWORD *)(USB2_CODE_BASE + 0x7E4))
#define USB2_CMD_LENGTH   *((volatile WORD *)(USB2_CODE_BASE + 0x7E8))
*/
static BYTE temp_usb_buffer[600];

int Usb2 :: create_pipe(int dev_addr, struct t_endpoint_descriptor *epd)
{
    DWORD pipe = 0L;
    
    // find free pipe
    int index = -1;
    for(int i=0;i<USB_MAX_PIPES;i++) {
        if(!pipes[i]) {
            index = i;
            break;
        }
    }
    if(index == -1)
        return -1;

    DWORD pipe_code = (DWORD)dev_addr;
    pipe_code |= ((int)(epd->endpoint_address & 0x0f)) << 7;
    pipe_code |= ((DWORD)le16_to_cpu(epd->max_packet_size)) << 16;

    pipes[index] = pipe_code;
    printf("Pipe with value %8x created. Index = %d.\n", pipe_code, index);
    return index;
}

void Usb2 :: free_pipe(int index)
{
    if(index >= USB_MAX_PIPES)
        return;
    if(index < 0)
        return;
    pipes[index] = 0;
}

int Usb2 :: control_exchange(int addr, void *out, int outlen, void *in, int inlen, BYTE **buf)
{
    // endpoint is 0
    DWORD dev_addr = (addr << 4) + 0 + 0x400000; // max length = 64, toggle = 0

    USB2_CMD_MEM_ADDR = (DWORD)out;
    USB2_CMD_DEV_ADDR = dev_addr;
    USB2_CMD_LENGTH   = outlen;
    USB2_CMD_CMD      = USB2_CMD_SETUP;
    
    while (USB2_CMD_CMD)
        ;
        
    DWORD d;
    get_fifo(&d);
    
    if (in) {
        USB2_CMD_MEM_ADDR = (DWORD)in;
    } else {
        USB2_CMD_MEM_ADDR = (DWORD)temp_usb_buffer;
    }
            
    USB2_CMD_LENGTH   = inlen;
     
    do {
        USB2_CMD_CMD      = USB2_CMD_DATA_IN;
        while (USB2_CMD_CMD)
            ;
        get_fifo(&d);
    } while(d == USB2_EVENT_RETRY);
        
    if (buf) {
        *buf = temp_usb_buffer;
    }
    return (int)USB2_CMD_TRANSFERRED;    
}

int Usb2 :: control_write(int addr, void *setup_out, int setup_len, void *data_out, int data_len)
{
    // endpoint is 0
    DWORD dev_addr = (addr << 4) + 0 + 0x400000; // max length = 64, toggle = 0

    USB2_CMD_MEM_ADDR = (DWORD)setup_out;
    USB2_CMD_DEV_ADDR = dev_addr;
    USB2_CMD_LENGTH   = setup_len;
    USB2_CMD_CMD      = USB2_CMD_SETUP;

    while (USB2_CMD_CMD)
        ;

    DWORD d;
    get_fifo(&d);

    if(!data_len)
        return 0;

    // now follow by out transaction
    USB2_CMD_MEM_ADDR = (DWORD)data_out;
    USB2_CMD_LENGTH   = data_len;

    do {
        USB2_CMD_CMD      = USB2_CMD_DATA_OUT;
        while (USB2_CMD_CMD)
            ;
        get_fifo(&d);
    } while(d == USB2_EVENT_RETRY);

    int transmitted = (int)USB2_CMD_TRANSFERRED;

    // and do a turnaround to indicate that we are done (and read status)
	USB2_CMD_MEM_ADDR = (DWORD)temp_usb_buffer;
    USB2_CMD_LENGTH   = 64;

    do {
        USB2_CMD_CMD      = USB2_CMD_DATA_IN;
        while (USB2_CMD_CMD)
            ;
        get_fifo(&d);
    } while(d == USB2_EVENT_RETRY);

    return transmitted;
}

int  Usb2 :: allocate_input_pipe(int len, int pipe, void(*callback)(BYTE *buf, int len, void *obj), void *object)
{
	for(int i=0;i<MAX_INPUT_PIPES;i++) {
		if (inputPipeControlWords[i] == 0) {
			// set inputPipeControlWords[i] to a sensible value
			inputPipeControlWords[i] = 1;
			inputPipeCallBacks[i] = callback;
			inputPipeObjects[i] = object;
			// write the inputPipeControlWord into the nano cpu memory space
			USB2_INPUT_PIPES[i] = pipes[pipe];
			return i;
		}
	}
	return -1;
}

void Usb2 :: free_input_pipe(int index)
{
	// check bounds
	if ((index < 0) || (index >= MAX_INPUT_PIPES))
		return;

	// write a zero to the nano cpu memory space to indicate a free slot
	USB2_INPUT_PIPES[index] = 0;
	inputPipeControlWords[index] = 0;
}

void Usb2 :: unstall_pipe(int pipe)
{
	printf("Unstalling pipe not yet supported. (Panic)");
	while(1)
		;
}

int  Usb2 :: bulk_out(void *buf, int len, int pipe)
{
    USB2_CMD_MEM_ADDR = (DWORD)buf;
    USB2_CMD_DEV_ADDR = pipes[pipe];
    USB2_CMD_LENGTH   = (DWORD)len;
    USB2_CMD_CMD      = USB2_CMD_DATA_OUT;

    while (USB2_CMD_CMD)
        ;

    DWORD d;
    get_fifo(&d); // from this code we should retrieve the toggle state and store it in pipes[pipe]

    return (int)USB2_CMD_TRANSFERRED;
}

int  Usb2 :: bulk_out_with_prefix(void *prefix, int prefix_len, void *buf, int len, int pipe)
{
	BYTE *pre_buf = (BYTE *)((BYTE *)buf - (BYTE *)prefix_len);
	BYTE *bprefix = (BYTE *)prefix;

	// backup and overwrite
	for(int i=0;i<prefix_len;i++) {
		temp_usb_buffer[i] = pre_buf[i];
		pre_buf[i] = bprefix[i];
	}

	// do the actual transfer with an adjusted buffer location
	int transferred = this->bulk_out(pre_buf, prefix_len + len, pipe);

	// backup and overwrite
	for(int i=0;i<prefix_len;i++) {
		pre_buf[i] = temp_usb_buffer[i];
	}
	return transferred;
}

int  Usb2 :: bulk_in(void *buf, int len, int pipe) // blocking
{
    USB2_CMD_MEM_ADDR = (DWORD)buf;
    USB2_CMD_DEV_ADDR = pipes[pipe];
    USB2_CMD_LENGTH   = (DWORD)len;
    USB2_CMD_CMD      = USB2_CMD_DATA_IN;

    while (USB2_CMD_CMD)
        ;

    DWORD d;
    get_fifo(&d); // from this code we should retrieve the toggle state and store it in pipes[pipe]

    return (int)USB2_CMD_TRANSFERRED;
}
