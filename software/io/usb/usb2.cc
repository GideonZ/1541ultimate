extern "C" {
	#include "itu.h"
    #include "dump_hex.h"
    #include "small_printf.h"
}
#include "usb2.h"
#include <stdlib.h>

extern BYTE  _binary_nano_minimal_b_start;
extern DWORD _binary_nano_minimal_b_size;

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
    prev_status = 0xFF;
    
    if(getFpgaCapabilities() & CAPAB_USB_HOST2) {
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

    // load the nano CPU code and start it.
    int size = (int)&_binary_nano_minimal_b_size;
    BYTE *src = &_binary_nano_minimal_b_start;
    BYTE *dst = (BYTE *)NANO_BASE;
    for(int i=0;i<size;i++)
        *(dst++) = *(src++);
    printf("Nano CPU based USB Controller: %d bytes loaded.\n", size);

    NANO_START = 1;

    initialized = true;
}

void Usb2 :: poll(Event &e)
{
	if(!initialized) {
		init();
        return;
    }
	BYTE usb_status = USB2_STATUS;
	if (prev_status != usb_status) {
		prev_status = usb_status;
		if (usb_status & USTAT_CONNECTED) {
			attach_root();
        } else {
            device_present = false;
            clean_up();
        }
    }
}

WORD *attr_fifo_data = (WORD *)ATTR_FIFO_BASE;

bool Usb2 :: get_fifo(WORD *out)
{
	WORD tail = ATTR_FIFO_TAIL;
	WORD head = ATTR_FIFO_HEAD;
	if (tail == head) {
		return false;
	}
	*out = attr_fifo_data[tail];
	tail ++;
	if (tail == ATTR_FIFO_ENTRIES)
		tail = 0;
	ATTR_FIFO_TAIL = tail;
	return true;
}

int Usb2 :: open_pipe()
{
	WORD *pipe = (WORD *)USB2_PIPES_BASE;
	for(int i=0;i<USB2_NUM_PIPES;i++) {
		if (*pipe == 0) {
			return i;
		}
		pipe += 8;
	}
	return -1;
}

void Usb2 :: init_pipe(int index, struct t_pipe *init)
{
	WORD *pipe = (WORD *)(USB2_PIPES_BASE + USB2_PIPE_SIZE * index);
	pipe[PIPE_OFFS_DevEP]    = init->DevEP;
	pipe[PIPE_OFFS_Length]   = init->Length;
	pipe[PIPE_OFFS_MaxTrans] = init->MaxTrans;
	pipe[PIPE_OFFS_Interval] = init->Interval;
	pipe[PIPE_OFFS_SplitCtl] = init->SplitCtl;
	pipe[PIPE_OFFS_Command]  = init->Command;
}

void Usb2 :: close_pipe(int pipe)
{
	WORD *p = (WORD *)USB2_PIPES_BASE;
	p[(8 * pipe)] = 0;
}

int Usb2 :: control_exchange(struct t_pipe *pipe, void *out, int outlen, void *in, int inlen)
{
	USB2_CMD_DevEP  = pipe->DevEP;
	USB2_CMD_Length = outlen;
	USB2_CMD_MaxTrans = pipe->MaxTrans;
	USB2_CMD_MemHi = ((DWORD)out) >> 16;
	USB2_CMD_MemLo = ((DWORD)out) & 0xFFFF;
	USB2_CMD_Command = UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_SETUP;

	// wait until it gets zero again
	while(USB2_CMD_Command)
		;

	// printf("Setup Result: %04x\n", USB2_CMD_Result);

	USB2_CMD_Length = inlen;
	WORD command = UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_IN | UCMD_TOGGLEBIT;// start with toggle bit 1

	if (in) {
		USB2_CMD_MemHi = ((DWORD)in) >> 16;
		USB2_CMD_MemLo = ((DWORD)in) & 0xFFFF;
		command |= UCMD_MEMWRITE;
	}
	USB2_CMD_Command = command;

	// wait until it gets zero again
	while(USB2_CMD_Command)
		;

	DWORD transferred = inlen - USB2_CMD_Length;
	// printf("In: %d bytes (Result = %4x)\n", transferred, USB2_CMD_Result);
	// dump_hex(read_buf, transferred);

	if (transferred) {
		USB2_CMD_Length = 0;
		USB2_CMD_Command = UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_OUT | UCMD_TOGGLEBIT; // send zero bytes as out packet

		// wait until it gets zero again
		while(USB2_CMD_Command)
			;
		// printf("Out Result = %4x\n", USB2_CMD_Result);
	}
	return transferred;
}

int Usb2 :: control_write(struct t_pipe *pipe, void *setup_out, int setup_len, void *data_out, int data_len)
{
	USB2_CMD_DevEP  = pipe->DevEP;
	USB2_CMD_Length = setup_len;
	USB2_CMD_MaxTrans = pipe->MaxTrans;
	USB2_CMD_MemHi = ((DWORD)setup_out) >> 16;
	USB2_CMD_MemLo = ((DWORD)setup_out) & 0xFFFF;
	USB2_CMD_Command = UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_SETUP;

	// wait until it gets zero again
	while(USB2_CMD_Command)
		;

	// printf("Setup Result: %04x\n", USB2_CMD_Result);

	USB2_CMD_Length = data_len;
	USB2_CMD_MemHi = ((DWORD)data_out) >> 16;
	USB2_CMD_MemLo = ((DWORD)data_out) & 0xFFFF;
	USB2_CMD_Command = UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_OUT | UCMD_TOGGLEBIT; // start with toggle bit 1;

	// wait until it gets zero again
	while(USB2_CMD_Command)
		;

	DWORD transferred = data_len - USB2_CMD_Length;
	// printf("Out: %d bytes (Result = %4x)\n", transferred, USB2_CMD_Result);
	// dump_hex(read_buf, transferred);

	USB2_CMD_Length = 0;
	USB2_CMD_Command = UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_IN | UCMD_TOGGLEBIT; // do an in transfer to end control write

	// wait until it gets zero again
	while(USB2_CMD_Command)
		;
	// printf("In Result = %4x\n", USB2_CMD_Result);
	return transferred;
}

int  Usb2 :: allocate_input_pipe(struct t_pipe *pipe, void(*callback)(BYTE *buf, int len, void *obj), void *object)
{
	int index = open_pipe();
	if (index < 0)
		return -1;

	inputPipeCallBacks[index] = callback;
	inputPipeObjects[index] = object;

	init_pipe(index, pipe);
	return -1;
}

void Usb2 :: free_input_pipe(int index)
{
	close_pipe(index);
	inputPipeCallBacks[index] = 0;
	inputPipeObjects[index] = 0;
}

void Usb2 :: unstall_pipe(struct t_pipe *pipe)
{
	printf("Unstalling pipe not yet supported. (Panic)");
	while(1)
		;
}

int  Usb2 :: bulk_out(struct t_pipe *pipe, void *buf, int len)
{
	printf("BULK OUT to %4x, len = %d\n", pipe->DevEP, len);
	DWORD addr = (DWORD)buf;
	int total_trans = 0;

	USB2_CMD_DevEP    = pipe->DevEP;
	USB2_CMD_MaxTrans = pipe->MaxTrans;
	USB2_CMD_MemHi = addr >> 16;
	USB2_CMD_MemLo = addr & 0xFFFF;

	do {
		int current_len = (len > 49152) ? 49152 : len;
		USB2_CMD_Length = current_len;
		USB2_CMD_Command = pipe->Command | UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_OUT;

		// wait until it gets zero again
		while(USB2_CMD_Command)
			;

		WORD result = USB2_CMD_Result;
		if (((result & URES_RESULT_MSK) != URES_ACK) && ((result & URES_RESULT_MSK) != URES_NYET)) {
			printf("Bulk Out error: $%4x, Transferred: %d\n", result, total_trans);
			break;
		}

		int transferred = current_len - USB2_CMD_Length;
		total_trans += transferred;
		addr += transferred;
		len -= transferred;
		pipe->Command = result & URES_TOGGLE; // that's what we start with next time.

	} while (len > 0);


	return total_trans;
}

/*int  Usb2 :: bulk_out_with_prefix(void *prefix, int prefix_len, void *buf, int len, int pipe)
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
}*/

int  Usb2 :: bulk_in(struct t_pipe *pipe, void *buf, int len) // blocking
{
	printf("BULK IN from %4x, len = %d\n", pipe->DevEP, len);
	DWORD addr = (DWORD)buf;
	int total_trans = 0;

	USB2_CMD_DevEP    = pipe->DevEP;
	USB2_CMD_MaxTrans = pipe->MaxTrans;
	USB2_CMD_MemHi = addr >> 16;
	USB2_CMD_MemLo = addr & 0xFFFF;

	do {
		int current_len = (len > 49152) ? 49152 : len;
		USB2_CMD_Length = current_len;
		WORD cmd = pipe->Command | UCMD_MEMWRITE | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_IN;
		printf("%d: %4x ", len, cmd);
		USB2_CMD_Command = cmd;

		// wait until it gets zero again
		while(USB2_CMD_Command)
			;

		WORD result = USB2_CMD_Result;
		if ((result & URES_RESULT_MSK) != URES_PACKET) {
			printf("Bulk IN error: $%4x, Transferred: %d\n", result, total_trans);
			break;
		}

		printf("Res = %4x\n", result);
		int transferred = current_len - USB2_CMD_Length;
		total_trans += transferred;
		addr += transferred;
		len -= transferred;
		pipe->Command = result & URES_TOGGLE; // that's what we start with next time.
	} while (len > 0);

	return total_trans;
}

void Usb2 :: bus_reset()
{
	NANO_DO_RESET = 1;

	for (int i=0; i<3; i++) {
		wait_ms(50);
		printf("Reset status: %b\n", USB2_STATUS);
		if (USB2_STATUS & USTAT_OPERATIONAL)
			break;
	}
}

UsbDevice *Usb2 :: init_simple(void)
{
	init();
	bus_reset();
	wait_ms(1000);
	BYTE usb_status = USB2_STATUS;
	if (usb_status & USTAT_CONNECTED) {
		UsbDevice *dev = new UsbDevice(this);
		if(dev) {
			if(!dev->init(1)) {
				delete dev;
				return NULL;
			}
		}
		return dev;
	}
	return NULL;
}
