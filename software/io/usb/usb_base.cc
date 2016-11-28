extern "C" {
	#include "itu.h"
    #include "dump_hex.h"
    #include "small_printf.h"
}
#include "usb_base.h"
#include "usb_device.h"
#include "task.h"
#include "profiler.h"

#include <stdlib.h>
#include <string.h>

#ifdef NIOS
#define nano_word(x) ((x >> 8) | ((x & 0xFF) << 8))
#else // assume big endian
#define nano_word(x) x
#endif

UsbBase usb2;

#define printf(...)

UsbBase :: UsbBase()
{
    ioWrite8(ITU_IRQ_DISABLE, ITU_INTERRUPT_USB);
	max_current = 500;
	remaining_current = 500;
	rootDevice = NULL;
	initialized = false;
	bus_speed = -1;

//	initHardware();
}

UsbBase :: ~UsbBase()
{
	deinitHardware();
}

UsbDevice *UsbBase :: first_device(void)
{
    return rootDevice;
}

/*
void UsbBase :: clean_up()
{
	rootDevice->deinstall();

	for(int i=0; i<128; i++) {
		deviceUsed[i] = 0;
	}
}
*/

// Called only from the Event context
int UsbBase :: get_device_slot(void)
{
    for(int i=1;i<128;i++) {
    	if(!deviceUsed[i]) {
    		deviceUsed[i] = 1;
    		return i;
    	}
    }
    return -1; // no empty slots
}
    

// Called only from the Event context
void UsbBase :: attach_root(void)
{
    printf("Attach root!!\n");
    rootDevice = NULL;
    remaining_current = 500; //max_current;
    bus_reset();

    UsbDevice *dev = new UsbDevice(this, get_bus_speed());
    if (install_device(dev, true)) {
    	rootDevice = dev;
    }
}

// Called only from the Event context
bool UsbBase :: install_device(UsbDevice *dev, bool draws_current)
{
    int idx = get_device_slot();
    char buf[16];
    dev->get_pathname(buf, 16);
    printf("Installing %s Parent = %p, ParentPort = %d\n", buf, dev->parent, dev->parent_port);

    bool ok = false;
    for(int i=0;i<5;i++) { // try 5 times!
        if(dev->init(idx)) {
            ok = true;
            break;
        } else {
        	wait_ms(100*i);
        }
    }
    if(!ok)
        return false;

    if(!draws_current) {
        dev->install();
        return true; // actually install should be able to return false too
    } else {
        int device_curr = int(dev->get_device_config()->max_power) * 2;

        if(device_curr > remaining_current) {
        	printf("Device current (%d mA) exceeds maximum remaining current (%d mA).\n", device_curr, remaining_current);
        }
        remaining_current -= device_curr;
        dev->install();
        return true; // actually install should be able to return false too
    }
    return false;
}

// Called from cleanup task.
void UsbBase :: deinstall_device(UsbDevice *dev)
{
    int addr = dev->current_address;
	dev->deinstall();
	delete dev;
	deviceUsed[addr] = 0;
}

////////////////////////////

extern uint8_t _nano_minimal_b_start;
extern uint32_t _nano_minimal_b_size;

uint16_t *attr_fifo_data = (uint16_t *)ATTR_FIFO_BASE;
uint16_t *block_fifo_data = (uint16_t *)BLOCK_FIFO_BASE;

static void poll_usb2(void *a)
{
	usb2.init();
	while(1) {
		usb2.poll();
	}
}

extern "C" BaseType_t usb_irq() {
	BaseType_t xHigherPriorityTaskWoken = usb2.irq_handler();
	// portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
	return xHigherPriorityTaskWoken;
}

void UsbBase :: initHardware()
{
	irq_count = 0;
	prev_status = 0xFF;
    blockBufferBase = NULL;
    circularBufferBase = NULL;

    if(getFpgaCapabilities() & CAPAB_USB_HOST2) {
    	mutex = xSemaphoreCreateMutex();
        queue = xQueueCreate(64, sizeof(struct usb_event));
        cleanup_queue = xQueueCreate(16, sizeof(UsbDevice *));

        NANO_START = 0;
        uint16_t *dst = (uint16_t *)NANO_BASE;
        for(int i=0; i<2048; i+=2) {
        	*(dst++) = 0;
        }
        ioWrite8(ITU_IRQ_DISABLE, ITU_INTERRUPT_USB);
        ioWrite8(ITU_IRQ_CLEAR, ITU_INTERRUPT_USB);

        xTaskCreate( poll_usb2, "USB Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
    } else {
        printf("No USB2 hardware found.\n");
    }
}

void UsbBase :: deinitHardware()
{
    ioWrite8(ITU_IRQ_DISABLE, ITU_INTERRUPT_USB);
	vSemaphoreDelete(mutex);
	vQueueDelete(queue);
    //clean_up();
	vQueueDelete(cleanup_queue);
}

// This STARTS the event context
void UsbBase :: input_task_start(void *u)
{
	UsbBase *usb = (UsbBase *)u;
	usb->input_task_impl();
}

// This RUNS the event context
void UsbBase :: input_task_impl(void)
{
	printf("USB input task is now running. this = %p\n", this);
	struct usb_event ev;
	while(1) {
		if (!queue) {
			printf("Bleh!\n");
			vTaskDelay(10);
			continue;
		}
		if (xQueueReceive(queue, &ev, 5000) == pdTRUE) {
			//printf("{%04x:%04x}", ev.fifo_word[0], ev.fifo_word[1]);
			PROFILER_SUB = 5;
			process_fifo(&ev);
		} else {
			printf("@");
		}
	}
}

// Called from event context
void UsbBase :: process_fifo(struct usb_event *ev)
{
	int pipe = ev->fifo_word[0] & 0x0F;
	int error = 0;

	if ((ev->fifo_word[0] & 0xFFF0) == 0xFFF0) {
		if (pipe == 15) {
			printf("USB Other IRQ. Status = %b\n", ev->fifo_word[1]);
			handle_status(ev->fifo_word[1]);
			return;
		} else {
			// pause_input_pipe(pipe);
			printf("USB Pipe Error. Pipe %d: Status: %4x\n", pipe, ev->fifo_word[1]);
			void *object = inputPipeObjects[pipe];
			if (object) {
				((UsbDriver *)object)->pipe_error(pipe);
			}
			return;
		}
	}

	uint8_t *buffer = 0;
	if (inputPipeBufferMethod[pipe] == e_block) {
		uint16_t idx = (ev->fifo_word[0] & 0xFFF0) / 384;
		free_map[idx] = 1; // allocated
		buffer = (uint8_t *) & blockBufferBase[ev->fifo_word[0] & 0xFFF0];
	} else {
		buffer = (uint8_t *) & circularBufferBase[ev->fifo_word[0] & 0xFFF0];
	}
	void *object = inputPipeObjects[pipe];

	PROFILER_SUB = 3;
	if (!object) {
		printf("** INVALID USB PACKET RECEIVED IN QUEUE! Pipe : %d\n", pipe);
		return;
	}
	inputPipeCallBacks[pipe](buffer, ev->fifo_word[1], object);
	PROFILER_SUB = 12;
}

// Called from event context
void UsbBase :: handle_status(uint16_t new_status)
{
	if (prev_status != new_status) {
		prev_status = new_status;
		if (new_status & USTAT_CONNECTED) {
			attach_root();
        } else {
        	if (rootDevice) {
//        		rootDevice->disable();
				queueDeinstall(rootDevice);
				rootDevice = NULL;
        	}
		}
    }
}

// Called from event context
void UsbBase :: queueDeinstall(UsbDevice *device)
{
	device->disable(); // do no more transactions, disable poll pipes, etc
	if (!cleanup_queue) {
		printf("CUleh!\n");
		return;
	}
	xQueueSend(cleanup_queue, &device, 500000);
}

// Called during init?  TODO
void UsbBase :: bus_reset()
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

// Called from where?  During Init, and from
// anyone that uses the blocks. In this case only the tcpip thread.
// But is there only one?  Should we protect this? TODO
bool UsbBase :: put_block_fifo(uint16_t in)
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


///////////////////////////////////////////////////////

// Called from poll / cleanup context
void UsbBase :: init(void)
{
    // clear our internal device list
    for(int i=0;i<128;i++) {
        deviceUsed[i] = 0;
    }
    rootDevice = NULL;

	NANO_START = 0;

	// initialize the buffers
    blockBufferBase = new uint32_t[384 * BLOCK_FIFO_ENTRIES];
    circularBufferBase = new uint8_t[4096];

    // load the nano CPU code and start it.
    int size = (int)&_nano_minimal_b_size;
    uint8_t *src = &_nano_minimal_b_start;
    uint16_t *dst = (uint16_t *)NANO_BASE;
    uint16_t temp;
    for(int i=0;i<size;i+=2) {
    	temp = (uint16_t(*(src++)) << 8);
    	temp |= uint16_t(*(src++));
    	*(dst++) = temp;
	}
    for(int i=size;i<2048;i+=2) {
    	*(dst++) = 0;
    }
    uint32_t *pul = (uint32_t *)NANO_BASE;
    uint32_t ul = *pul;
    printf("Nano CPU based USB Controller: %d bytes loaded from %p. First DW: %08x\n", size, &_nano_minimal_b_start, ul);

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
	state = 0;
	ioWrite8(ITU_IRQ_CLEAR,  0x04);
	ioWrite8(ITU_IRQ_ENABLE, 0x04);
	NANO_START = 1;

    printf("Queue = %p. Creating USB task. This = %p\n", queue, this);
	xTaskCreate( UsbBase :: input_task_start, "USB Input Event Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 4, NULL );

	ioWrite8(ITU_MISC_IO, 1); // coherency is on
}

// Called from poll / cleanup context
void UsbBase :: poll()
{
	UsbDevice *device;
	BaseType_t doCleanup;

	do {
		if (!cleanup_queue) {
			printf("CURBleh\n");
		}
		doCleanup = xQueueReceive(cleanup_queue, &device, 40);
		if (doCleanup == pdTRUE) {
			deinstall_device(device);
		}
	} while(doCleanup == pdTRUE);

	if(rootDevice) {
		UsbDriver *drv = rootDevice->driver;
		if (drv) {
			drv->poll();
		}
	}
}

void UsbBase :: deinit(void)
{
	power_off();

	NANO_START = 0;

	// clear RAM
	uint8_t *dst = (uint8_t *)NANO_BASE;
    for(int i=0;i<2048;i++)
        *(dst++) = 0;
}

// Called from protected function 'allocate input pipe'
int UsbBase :: open_pipe()
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

// Called from protected function 'allocate input pipe'
void UsbBase :: init_pipe(int index, struct t_pipe *init)
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

// Still used? -> No
void UsbBase :: pause_input_pipe(int index)
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

// Called from event context (pipe input processor)
void UsbBase :: resume_input_pipe(int index)
{
	volatile uint16_t *pipe = (uint16_t *)(USB2_PIPES_BASE + USB2_PIPE_SIZE * index);
	uint16_t command = pipe[PIPE_OFFS_Command];
	command &= ~UCMD_PAUSED;
	pipe[PIPE_OFFS_Command] = command;
	//printf("Input pipe %d resumed: %04x\n", index, pipe[PIPE_OFFS_Command]);
}

void UsbBase :: free_input_buffer(int inpipe, uint8_t *buffer)
{
	if (inputPipeBufferMethod[inpipe] != e_block)
		return;

	// block returned to queue!
	unsigned int offset = (unsigned int)buffer - (unsigned int)blockBufferBase;
	put_block_fifo(uint16_t(offset >> 2));
}

void UsbBase :: close_pipe(int pipe)
{
	uint16_t *p = (uint16_t *)USB2_PIPES_BASE;
	p[(8 * pipe)] = 0;
}

// Reentrant
uint16_t UsbBase :: getSplitControl(int addr, int port, int speed, int type)
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

// Protected by Mutex
void UsbBase :: power_off()
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return;
    }

    printf("Turning USB bus off...\n");
    USB2_CMD_Command = UCMD_OFF;
    vTaskDelay(10);
    USB2_CMD_Command = 0;

    xSemaphoreGive(mutex);
}

// Protected by Mutex
int UsbBase :: control_exchange(struct t_pipe *pipe, void *out, int outlen, void *in, int inlen)
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

// Protected by Mutex
int UsbBase :: control_write(struct t_pipe *pipe, void *setup_out, int setup_len, void *data_out, int data_len)
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

// Protected by Mutex
int  UsbBase :: allocate_input_pipe(struct t_pipe *pipe, void(*callback)(uint8_t *buf, int len, void *obj), void *object)
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

void UsbBase :: free_input_pipe(int index)
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

int  UsbBase :: bulk_out(struct t_pipe *pipe, void *buf, int len)
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return -9;
    }
	// printf("BULK OUT to %4x, len = %d\n", pipe->DevEP, len);

	uint8_t sub = PROFILER_SUB; PROFILER_SUB = 13;

    if (((uint32_t)buf) & 1) {
    	printf("Bulk_out: Unaligned buffer %p\n", buf);
    	while(1);
    }

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

int  UsbBase :: bulk_in(struct t_pipe *pipe, void *buf, int len) // blocking
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return -9;
    }

    if (((uint32_t)buf) & 3) {
    	printf("Bulk_in: Unaligned buffer %p\n", buf);
    	while(1);
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
			//printf("CMD res: %4x\n", result);
			//dump_hex_relative(buf, transferred);
			//total_trans = -8;
			break;
		}
	} while (len > 0);

    // printf("Bulk in done: %d\n", total_trans);
    xSemaphoreGive(mutex);

    return total_trans;
}

/*
UsbDevice *UsbBase :: init_simple(void)
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
*/

////////////////////////////////////////////////////////////

// Called from IRQ
BaseType_t UsbBase :: irq_handler(void)
{
	PROFILER_SUB = 2;
	irq_count++;
	uint16_t read;

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	while (get_fifo(&read)) {
		if (read == 0xFFFF) {
			event.fifo_word[0] = read;
			event.fifo_word[1] = USB2_STATUS;
			state = 2;
		} else {
			event.fifo_word[state++] = read;
		}

		if (state == 2) {
			state = 0;
			//printf("[%04x:%04x]", event.fifo_word[0], event.fifo_word[1]);
			if (!queue) {
				ioWrite8(UART_DATA, '#');
			} else {
				xQueueSendFromISR(queue, &event, &xHigherPriorityTaskWoken);
			}
		}
	}
	PROFILER_SUB = 0;
	return xHigherPriorityTaskWoken;
}

// Called from IRQ
bool UsbBase :: get_fifo(uint16_t *out)
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
