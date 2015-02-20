/*
 * usb_test.c
 *
 *  Created on: Feb 4, 2015
 *      Author: Gideon
 */


#include "small_printf.h"
#include "itu.h"
#include "usb_nano.h"
#include "dump_hex.h"
#include <string.h>

void myISR(void) __attribute__ ((interrupt_handler));

const BYTE c_get_device_descriptor[] = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00 };
const BYTE c_get_config_descriptor[] = { 0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0x80, 0x00 };
const BYTE c_set_address[]           = { 0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
const BYTE c_set_configuration[]     = { 0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };

int irq_count;
BYTE big_buffer[16384];

WORD *attr_fifo_data = (WORD *)ATTR_FIFO_BASE;

WORD usb_read_queue()
{
	WORD tail = ATTR_FIFO_TAIL;
	WORD head = ATTR_FIFO_HEAD;
	if (tail == head) {
		return 0xFFFF;
	}
	WORD ret = attr_fifo_data[tail];
	tail ++;
	if (tail == ATTR_FIFO_ENTRIES)
		tail = 0;
	ATTR_FIFO_TAIL = tail;
	return ret;
}

int mouse_x, mouse_y;

void usb_irq() {
	WORD read1, read2;
	read1 = usb_read_queue();
	read2 = usb_read_queue();
	if ((read1 & 0xFFF0) == 0xFFF0) {
		int pipe = read1 & 0x0F;
		if (pipe == 15) {
			printf("USB Other IRQ. Status = %b\n", USB2_STATUS);
		} else {
			printf("USB Pipe Error. Pipe %d: Status: %4x\n", pipe, read2);
		}
	} else {
		dump_hex(&big_buffer[read1], read2);
//		mouse_x += (char)big_buffer[read1+1];
//		mouse_y += (char)big_buffer[read1+2];
//		printf("%5d,%5d\r", mouse_x, mouse_y);
	}
	//printf("USB interrupt! %4x %4x\n", usb_read_queue(), usb_read_queue());
}

void myISR(void)
{
	BYTE act = ITU_IRQ_ACTIVE;
	if (act & 1) {
		irq_count ++;
	}
	if (act & 4) {
		usb_irq();
	}
	ITU_IRQ_CLEAR = act;
}

int enable_irqs()
{
    puts("Hello IRQs!");

    // store interrupt handler in the right trap, for simulation this should work.
    unsigned int pointer = (unsigned int)&myISR;
    unsigned int *vector = (unsigned int *)0x10;
    vector[0] = (0xB0000000 | (pointer >> 16));
    vector[1] = (0xB8080000 | (pointer & 0xFFFF));

    // enable interrupts
    __asm__ ("msrset r0, 0x02");

    ITU_IRQ_TIMER_LO  = 0x4B;
	ITU_IRQ_TIMER_HI  = 0x4C;
    ITU_IRQ_TIMER_EN  = 0x01;
    ITU_IRQ_ENABLE    = 0x05; // timer and usb interrupts

    return 0;
}

void usb_control_transfer(WORD DevEP, WORD maxtrans, const void *setup, void *read_buf, WORD read_len) {

	USB2_CMD_DevEP  = DevEP; // device 0, endpoint 0
	USB2_CMD_Length = 8;
	USB2_CMD_MaxTrans = maxtrans;
	USB2_CMD_MemHi = ((DWORD)setup) >> 16;
	USB2_CMD_MemLo = ((DWORD)setup) & 0xFFFF;
	USB2_CMD_Command = UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_SETUP;

	// wait until it gets zero again
	while(USB2_CMD_Command)
		;

	printf("Setup Result: %04x\n", USB2_CMD_Result);

	USB2_CMD_Length = read_len;
	USB2_CMD_MemHi = ((DWORD)read_buf) >> 16;
	USB2_CMD_MemLo = ((DWORD)read_buf) & 0xFFFF;
	USB2_CMD_Command = UCMD_MEMWRITE | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_IN | UCMD_TOGGLEBIT; // start with toggle bit 1

	// wait until it gets zero again
	while(USB2_CMD_Command)
		;

	DWORD transferred = read_len - USB2_CMD_Length;
	printf("In: %d bytes (Result = %4x)\n", transferred, USB2_CMD_Result);
	dump_hex(read_buf, transferred);

	if (transferred) {
		USB2_CMD_Length = 0;
		USB2_CMD_Command = UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_OUT | UCMD_TOGGLEBIT; // send zero bytes as out packet

		// wait until it gets zero again
		while(USB2_CMD_Command)
			;
		printf("Out Result = %4x\n", USB2_CMD_Result);
	}
}

int open_pipe(struct t_pipe *init)
{
	WORD *pipe = (WORD *)USB2_PIPES_BASE;
	for(int i=0;i<USB2_NUM_PIPES;i++) {
		if (*pipe == 0) {
			pipe[PIPE_OFFS_DevEP]    = init->DevEP;
			pipe[PIPE_OFFS_Length]   = init->Length;
			pipe[PIPE_OFFS_MaxTrans] = init->MaxTrans;
			pipe[PIPE_OFFS_Interval] = init->Interval;
			pipe[PIPE_OFFS_SplitCtl] = init->SplitCtl;
			pipe[PIPE_OFFS_Command]  = init->Command;
			return i;
		}
		pipe += 8;
	}
	return -1;
}

int close_pipe(int pipe)
{
	WORD *p = (WORD *)USB2_PIPES_BASE;
	p[(8 * pipe)] = 0;
	return pipe;
}

int main()
{
	DWORD small_buffer[32];

	struct t_pipe my_pipe;

	//NANO_SIMULATION = 1;

    enable_irqs();

    NANO_START = 1;

	puts("Hello USB2!");

	USB2_CIRC_MEMADDR_START_HI  = ((DWORD)&big_buffer) >> 16;
	USB2_CIRC_MEMADDR_START_LO  = ((DWORD)&big_buffer) & 0xFFFF;
	USB2_CIRC_BUF_SIZE		  	= 16384;
	USB2_CIRC_BUF_OFFSET		= 0;

	my_pipe.Command  = UCMD_MEMWRITE | UCMD_USE_CIRC | UCMD_DO_DATA | UCMD_IN;
	my_pipe.DevEP    = 0x0101;
	my_pipe.Length   = 4;
	my_pipe.MaxTrans = 8;
	my_pipe.Interval = 200; // 40 Hz
	my_pipe.SplitCtl = 0;

	while(1) {
		do {
			wait_ms(1000);
			printf("Status: %b %d\n", USB2_STATUS, irq_count);
		} while(!(USB2_STATUS & USTAT_CONNECTED));

		NANO_DO_RESET = 1;

		for (int i=0; i<3; i++) {
			wait_ms(50);
			printf("Reset status: %b\n", USB2_STATUS);
			if (USB2_STATUS & USTAT_OPERATIONAL)
				break;
		}

		int speed = NANO_LINK_SPEED;
		int maxtrans;
		switch(speed) {
		case 0:
			puts("Low speed.");
			maxtrans = 8;
			break;
		case 1:
			puts("Full speed.");
			maxtrans = 64;
			break;
		case 2:
			puts("**High Speed! **");
			maxtrans = 64;
			break;
		default:
			printf("ERROR! Speed = %d\n", speed);
		}

		usb_control_transfer(0, maxtrans, c_get_device_descriptor, small_buffer, 128);
		usb_control_transfer(0, maxtrans, c_set_address, small_buffer, 128);
		usb_control_transfer(0x0100, maxtrans, c_get_config_descriptor, small_buffer, 128);
		usb_control_transfer(0x0100, maxtrans, c_set_configuration, small_buffer, 128);

		int pipe = 0;
		if (speed == 0) { // low speed device, so we assume it is a mouse
			pipe = open_pipe(&my_pipe);
		}

		do {
			wait_ms(1);
//			printf("Status: %b %4x\n", USB2_STATUS, USB2_CIRC_BUF_OFFSET);
		} while(USB2_STATUS & USTAT_CONNECTED);

		close_pipe(pipe);

		puts("Disconnected!");
	}

    return 0;
}
