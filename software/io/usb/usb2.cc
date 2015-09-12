#include <stdlib.h>
#include <string.h>

extern "C" {
	#include "itu.h"
    #include "dump_hex.h"
    #include "small_printf.h"
}
#include "usb2.h"
#include "task.h"
#include "profiler.h"

extern uint8_t  _nano_minimal_b_start;
extern uint32_t _nano_minimal_b_size;

uint16_t *attr_fifo_data = (uint16_t *)ATTR_FIFO_BASE;
uint16_t *block_fifo_data = (uint16_t *)BLOCK_FIFO_BASE;

Usb2 usb2; // the global

volatile int irq_count;

__inline uint16_t le16_to_cpu(uint16_t h)
{
    return (h >> 8) | (h << 8);
}

static void poll_usb2(void *a)
{
	usb2.init();
	while(1) {
		usb2.poll();
		vTaskDelay(20);
	}
}

extern "C" BaseType_t usb_irq() {
	return usb2.irq_handler();
}

struct usb_packet {
	uint8_t *data;
	void *object;
	uint16_t  length;
	uint16_t  pipe;
};

Usb2 :: Usb2()
{
    prev_status = 0xFF;
    blockBufferBase = NULL;
    circularBufferBase = NULL;
    
    if(getFpgaCapabilities() & CAPAB_USB_HOST2) {
    	mutex = xSemaphoreCreateMutex();
        queue = xQueueCreate(64, sizeof(struct usb_packet));

        xTaskCreate( poll_usb2, "\006USB Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
    }
}

Usb2 :: ~Usb2()
{
	vSemaphoreDelete(mutex);
	vQueueDelete(queue);
    clean_up();
}

void Usb2 :: input_task_start(void *u)
{
	Usb2 *usb = (Usb2 *)u;
	usb->input_task_impl();
}

void Usb2 :: input_task_impl(void)
{
	printf("USB input task is now running. this = %p\n", this);
	struct usb_packet pkt;
	while(1) {
		if (xQueueReceive(queue, &pkt, 5000) == pdTRUE) {
			PROFILER_SUB = 5;
			if (!pkt.data) { // error code
				((UsbDriver *)pkt.object)->pipe_error(pkt.pipe);
			} else {
				inputPipeCallBacks[pkt.pipe](pkt.data, pkt.length, pkt.object);
			}
			PROFILER_SUB = 12;
		} else {
			printf("@");
		}
	}
}

void Usb2 :: init(void)
{
    // clear our internal device list
    for(int i=0;i<USB_MAX_DEVICES;i++) {
        device_list[i] = NULL;
    }
    device_present = false;

    // initialize the buffers
    blockBufferBase = new uint32_t[384 * BLOCK_FIFO_ENTRIES];
    circularBufferBase = new uint8_t[4096];

    // load the nano CPU code and start it.
    int size = (int)&_nano_minimal_b_size;
    uint8_t *src = &_nano_minimal_b_start;
    uint8_t *dst = (uint8_t *)NANO_BASE;
    for(int i=0;i<size;i++)
        *(dst++) = *(src++);
    printf("Nano CPU based USB Controller: %d bytes loaded.\n", size);
    printf("Circular Buffer Base = %p\n", circularBufferBase);
    memset (circularBufferBase, 0xe0, 4096);

	USB2_CIRC_MEMADDR_START_HI  = ((uint32_t)circularBufferBase) >> 16;
	USB2_CIRC_MEMADDR_START_LO  = ((uint32_t)circularBufferBase) & 0xFFFF;
	USB2_CIRC_BUF_SIZE		  	= 4096;
	USB2_CIRC_BUF_OFFSET		= 0;
	USB2_BLOCK_BASE_HI 			= ((uint32_t)blockBufferBase) >> 16;
	USB2_BLOCK_BASE_LO  		= ((uint32_t)blockBufferBase) & 0xFFFF;

	for (int i=0;i<BLOCK_FIFO_ENTRIES;i++) {
		block_fifo_data[i] = uint16_t(i * 384);
		free_map[i] = 0;
	}
	BLOCK_FIFO_HEAD = BLOCK_FIFO_ENTRIES - 1;

	NANO_START = 1;

    printf("Queue = %p. Creating USB task. This = %p\n", queue, this);
	xTaskCreate( Usb2 :: input_task_start, "\004USB Input Event Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 4, NULL );

	ioWrite8(ITU_MISC_IO, 1); // coherency is on
}

void Usb2 :: deinit(void)
{
	NANO_START = 0;

	// clear RAM
	uint8_t *dst = (uint8_t *)NANO_BASE;
    for(int i=0;i<2048;i++)
        *(dst++) = 0;

}

void Usb2 :: poll()
{
	uint8_t usb_status = USB2_STATUS;
	if (prev_status != usb_status) {
		prev_status = usb_status;
		if (usb_status & USTAT_CONNECTED) {
			if (!device_present) {
				device_present = true;
				attach_root();
			}
        } else {
        	if (device_present) {
				device_present = false;
				clean_up();
        	}
		}
    }
	for(int i=0;i<USB_MAX_DEVICES;i++) {
		UsbDevice *dev = device_list[i];
		if(dev) {
			UsbDriver *drv = dev->driver;
			if(drv) {
				drv->poll();
			}
		}
	}
}

BaseType_t Usb2 :: irq_handler(void)
{
	PROFILER_SUB = 2;
	uint16_t read1, read2;
	bool success = true;
	success &= get_fifo(&read1);
	success &= get_fifo(&read2);

	if (!success) {
		printf(":(");
		PROFILER_SUB = 0;
		return pdFALSE;
	}

	int pipe = read1 & 0x0F;
	int error = 0;
	if ((read1 & 0xFFF0) == 0xFFF0) {
		if (pipe == 15) {
			printf("USB Other IRQ. Status = %b\n", USB2_STATUS);
			return pdFALSE;
		} else {
			pause_input_pipe(pipe);
			printf("USB Pipe Error. Pipe %d: Status: %4x\n", pipe, read2);
			error = 1;
		}
	}
	//printf("%4x,%4x,%d\n", read1, read2, success);

	uint8_t *buffer = 0;
	if (!error) {
		if (inputPipeBufferMethod[pipe] == e_block) {
			uint16_t idx = (read1 & 0xFFF0) / 384;
			free_map[idx] = 1; // allocated
			buffer = (uint8_t *) & blockBufferBase[read1 & 0xFFF0];
		} else {
			buffer = (uint8_t *) & circularBufferBase[read1 & 0xFFF0];
		}
	}

	struct usb_packet pkt;
	pkt.data = buffer; // will be 0 upon error!
	pkt.length = read2;
	pkt.object = inputPipeObjects[pipe];
	pkt.pipe = pipe;
//#ifndef OS
//	inputPipeCallBacks[pipe](pkt.data, pkt.length, pkt.object);

	BaseType_t retval;
	PROFILER_SUB = 3;
	xQueueSendFromISR(queue, &pkt, &retval);
	PROFILER_SUB = 4;
	return retval;
}

bool Usb2 :: get_fifo(uint16_t *out)
{
	uint16_t tail = ATTR_FIFO_TAIL;
	uint16_t head;

	do {
		head = ATTR_FIFO_HEAD;
	} while(head != ATTR_FIFO_HEAD);

//	printf("Head: %d, Tail: %d\n", head, tail);
	if (tail == head) {
		*out = 0xFFFF;
		return false;
	}
	*out = attr_fifo_data[tail];
	tail ++;
	if (tail == ATTR_FIFO_ENTRIES)
		tail = 0;
	ATTR_FIFO_TAIL = tail;
	return true;
}


bool Usb2 :: put_block_fifo(uint16_t in)
{
	uint16_t tail;

	do {
		tail = BLOCK_FIFO_TAIL;
	} while(tail != BLOCK_FIFO_TAIL);

	uint16_t idx = (in / 384);
	if (!free_map[idx]) {
		ioWrite8(UART_DATA, '^');
		return true;
	}
	free_map[idx] = 0;

	uint16_t head = BLOCK_FIFO_HEAD;
	uint16_t head_next = head + 1;
	if (head_next == BLOCK_FIFO_ENTRIES)
		head_next = 0;
	if (head_next == tail) {
		printf("block fifo full! free dropped\n");
		return false;
	}

	block_fifo_data[head] = in;
	BLOCK_FIFO_HEAD = head_next;
	return true;
}

int Usb2 :: open_pipe()
{
	uint16_t *pipe = (uint16_t *)USB2_PIPES_BASE;
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
	uint16_t *pipe = (uint16_t *)(USB2_PIPES_BASE + USB2_PIPE_SIZE * index);
	pipe[PIPE_OFFS_DevEP]    = init->DevEP;
	pipe[PIPE_OFFS_Length]   = init->Length;
	pipe[PIPE_OFFS_MaxTrans] = init->MaxTrans;
	pipe[PIPE_OFFS_Interval] = init->Interval;
	pipe[PIPE_OFFS_SplitCtl] = init->SplitCtl;
	if (init->Command) {
		pipe[PIPE_OFFS_Command]  = init->Command;
		inputPipeCommand[index] = init->Command;
	} else {
		uint16_t command = UCMD_MEMWRITE | UCMD_DO_DATA | UCMD_IN;
		if (init->Length <= 16) {
			command |=  UCMD_USE_CIRC;
			inputPipeBufferMethod[index] = e_circular;
		} else {
			command |= UCMD_USE_BLOCK;
			inputPipeBufferMethod[index] = e_block;
		}
		pipe[PIPE_OFFS_Command] = command;
		inputPipeCommand[index] = command;
	}
}

void Usb2 :: pause_input_pipe(int index)
{
	uint16_t *pipe = (uint16_t *)(USB2_PIPES_BASE + USB2_PIPE_SIZE * index);
	uint16_t command = pipe[PIPE_OFFS_Command];
	if (command & UCMD_PAUSED) {
		printf("Pause was already set\n");
		return;
	}
	command |= UCMD_PAUSED;
	pipe[PIPE_OFFS_Command] = command;
}

void Usb2 :: resume_input_pipe(int index)
{
	uint16_t *pipe = (uint16_t *)(USB2_PIPES_BASE + USB2_PIPE_SIZE * index);
	uint16_t command = pipe[PIPE_OFFS_Command];
	command &= ~UCMD_PAUSED;
	pipe[PIPE_OFFS_Command] = command;
}

void Usb2 :: free_input_buffer(int inpipe, uint8_t *buffer)
{
	if (inputPipeBufferMethod[inpipe] != e_block)
		return;

	// block returned to queue!
	unsigned int offset = (unsigned int)buffer - (unsigned int)blockBufferBase;
	put_block_fifo(uint16_t(offset >> 2));
}


void Usb2 :: close_pipe(int pipe)
{
	uint16_t *p = (uint16_t *)USB2_PIPES_BASE;
	p[(8 * pipe)] = 0;
}

uint16_t Usb2 :: getSplitControl(int addr, int port, int speed, int type)
{
	uint16_t retval = SPLIT_DO_SPLIT | (addr << 8) | (port & 0x0F) | ((type & 0x03) << 4);
	switch(speed) {
	case 1:
		break;
	case 0:
		retval |= SPLIT_SPEED;
		break;
	default:
		retval = 0;
	}
	return retval;
}

int Usb2 :: control_exchange(struct t_pipe *pipe, void *out, int outlen, void *in, int inlen)
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return -9;
    }

	USB2_CMD_DevEP  = pipe->DevEP;
	USB2_CMD_Length = outlen;
	USB2_CMD_MaxTrans = pipe->MaxTrans;
	USB2_CMD_MemHi = ((uint32_t)out) >> 16;
	USB2_CMD_MemLo = ((uint32_t)out) & 0xFFFF;
	USB2_CMD_SplitCtl = pipe->SplitCtl;
	USB2_CMD_Command = UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_SETUP;

	// wait until it gets zero again
	while(USB2_CMD_Command)
		;

	// printf("Setup Result: %04x\n", USB2_CMD_Result);

	USB2_CMD_Length = inlen;
	uint16_t command = UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_IN | UCMD_TOGGLEBIT;// start with toggle bit 1

	if (in) {
		USB2_CMD_MemHi = ((uint32_t)in) >> 16;
		USB2_CMD_MemLo = ((uint32_t)in) & 0xFFFF;
		command |= UCMD_MEMWRITE;
	}
	USB2_CMD_Command = command;

	// wait until it gets zero again
	while(USB2_CMD_Command)
		;

	uint32_t transferred = inlen - USB2_CMD_Length;
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
    xSemaphoreGive(mutex);
	return transferred;
}

int Usb2 :: control_write(struct t_pipe *pipe, void *setup_out, int setup_len, void *data_out, int data_len)
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return -9;
    }
	USB2_CMD_DevEP  = pipe->DevEP;
	USB2_CMD_Length = setup_len;
	USB2_CMD_MaxTrans = pipe->MaxTrans;
	USB2_CMD_MemHi = ((uint32_t)setup_out) >> 16;
	USB2_CMD_MemLo = ((uint32_t)setup_out) & 0xFFFF;
	USB2_CMD_SplitCtl = pipe->SplitCtl;
	USB2_CMD_Command = UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_SETUP;

	// wait until it gets zero again
	while(USB2_CMD_Command)
		;

	// printf("Setup Result: %04x\n", USB2_CMD_Result);

	USB2_CMD_Length = data_len;
	USB2_CMD_MemHi = ((uint32_t)data_out) >> 16;
	USB2_CMD_MemLo = ((uint32_t)data_out) & 0xFFFF;
	USB2_CMD_Command = UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_OUT | UCMD_TOGGLEBIT; // start with toggle bit 1;

	// wait until it gets zero again
	while(USB2_CMD_Command)
		;

	uint32_t transferred = data_len - USB2_CMD_Length;
	// printf("Out: %d bytes (Result = %4x)\n", transferred, USB2_CMD_Result);
	// dump_hex(read_buf, transferred);

	USB2_CMD_Length = 0;
	USB2_CMD_Command = UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_IN | UCMD_TOGGLEBIT; // do an in transfer to end control write

	// wait until it gets zero again
	while(USB2_CMD_Command)
		;
	// printf("In Result = %4x\n", USB2_CMD_Result);
    xSemaphoreGive(mutex);
	return transferred;
}

int  Usb2 :: allocate_input_pipe(struct t_pipe *pipe, void(*callback)(uint8_t *buf, int len, void *obj), void *object)
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return -9;
    }
	int index = open_pipe();
	if (index >= 0) {
		inputPipeCallBacks[index] = callback;
		inputPipeObjects[index] = object;

		init_pipe(index, pipe);
	}
/*
	for(int i=0;i<8;i++) {
		printf(" OBJ: %p CB: %p CMB: %4x\n", inputPipeObjects[i], inputPipeCallBacks[i], inputPipeCommand[i]);
	}
*/
	xSemaphoreGive(mutex);
	return index;
}

void Usb2 :: free_input_pipe(int index)
{
	if (index < 0) {
		return;
	}
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return;
    }
	close_pipe(index);
	inputPipeCallBacks[index] = 0;
	inputPipeObjects[index] = 0;
    xSemaphoreGive(mutex);
}

int  Usb2 :: bulk_out(struct t_pipe *pipe, void *buf, int len)
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return -9;
    }
	// printf("BULK OUT to %4x, len = %d\n", pipe->DevEP, len);

	uint8_t sub = PROFILER_SUB; PROFILER_SUB = 13;

    uint32_t addr = (uint32_t)buf;
	int total_trans = 0;

	USB2_CMD_DevEP    = pipe->DevEP;
	USB2_CMD_MaxTrans = pipe->MaxTrans;
	USB2_CMD_SplitCtl = pipe->SplitCtl;
	USB2_CMD_MemHi = addr >> 16;
	USB2_CMD_MemLo = addr & 0xFFFF;

	do {
		int current_len = (len > 49152) ? 49152 : len;
		USB2_CMD_Length = current_len;
		uint16_t cmd = pipe->Command | UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_OUT;
		//printf("%d: %4x ", len, cmd);
		USB2_CMD_Command = cmd;

		// wait until it gets zero again
		while(USB2_CMD_Command)
			;

		uint16_t result = USB2_CMD_Result;
		if (((result & URES_RESULT_MSK) != URES_ACK) && ((result & URES_RESULT_MSK) != URES_NYET)) {
			printf("Bulk Out error: $%4x, Transferred: %d\n", result, total_trans);
			break;
		}

		//printf("Res = %4x\n", result);
		int transferred = current_len - USB2_CMD_Length;
		total_trans += transferred;
		addr += transferred;
		len -= transferred;
		pipe->Command = (result & URES_TOGGLE); // that's what we start with next time.

	} while (len > 0);

    // printf("Bulk out done: %d\n", total_trans);
    xSemaphoreGive(mutex);

    PROFILER_SUB = sub;
	return total_trans;
}

int  Usb2 :: bulk_in(struct t_pipe *pipe, void *buf, int len) // blocking
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return -9;
    }

    // printf("BULK IN from %4x, len = %d\n", pipe->DevEP, len);
	uint32_t addr = (uint32_t)buf;
	int total_trans = 0;

	USB2_CMD_DevEP    = pipe->DevEP;
	USB2_CMD_MaxTrans = pipe->MaxTrans;
	USB2_CMD_SplitCtl = pipe->SplitCtl;
	USB2_CMD_MemHi = addr >> 16;
	USB2_CMD_MemLo = addr & 0xFFFF;

	do {
		int current_len = (len > 49152) ? 49152 : len;
		USB2_CMD_Length = current_len;
		uint16_t cmd = pipe->Command | UCMD_MEMWRITE | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_IN;
		//printf("%d: %4x \n", len, cmd);
		USB2_CMD_Command = cmd;

		// wait until it gets zero again
		while(USB2_CMD_Command)
			;

		uint16_t result = USB2_CMD_Result;
		if ((result & URES_RESULT_MSK) != URES_PACKET) {
			printf("Bulk IN error: $%4x, Transferred: %d\n", result, total_trans);
			if ((result & URES_RESULT_MSK) == URES_STALL) {
				total_trans = -4; // error code
			} else {
				total_trans = -1; // error code
			}
			break;
		}

		//printf("Res = %4x\n", result);
		int transferred = current_len - USB2_CMD_Length;
		total_trans += transferred;
		addr += transferred;
		len -= transferred;
		pipe->Command = (result & URES_TOGGLE) ^ URES_TOGGLE; // that's what we start with next time.
		if (transferred != current_len) { // some bytes remained?
			printf("CMD res: %4x\n", result);
			total_trans = -8;
			break;
		}
	} while (len > 0);

    // printf("Bulk in done: %d\n", total_trans);
    xSemaphoreGive(mutex);
	return total_trans;
}

void Usb2 :: bus_reset()
{
	NANO_DO_RESET = 1;

	for (int i=0; i<3; i++) {
		wait_ms(50);
		printf("Reset status: %b Speed: %b\n", USB2_STATUS, NANO_LINK_SPEED);
		if (USB2_STATUS & USTAT_OPERATIONAL)
			break;
	}
	set_bus_speed(NANO_LINK_SPEED);
}

UsbDevice *Usb2 :: init_simple(void)
{
	init();
	bus_reset();
	wait_ms(1000);
	uint8_t usb_status = USB2_STATUS;
	if (usb_status & USTAT_CONNECTED) {
		UsbDevice *dev = new UsbDevice(this, get_bus_speed());
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
