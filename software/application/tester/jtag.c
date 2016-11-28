/*
 * jtag.c
 *
 *  Created on: Nov 13, 2016
 *      Author: gideon
 */

#include <stdint.h>
#include <stdio.h>
#include "jtag.h"

#define JTAG_LAST   0x40000000
#define JTAG_TMSSEL 0x80000000
#define JTAG_MAXRUN 16384

void jtag_reset_to_idle(volatile uint32_t *host)
{
	host[0] = JTAG_TMSSEL | (5 << 16) | 0x1F; // send 6 bits: 1, 1, 1, 1, 1, 0
}

// Precondition: We are in Idle state
void jtag_instruction(volatile uint32_t *host, uint32_t inst)
{
	host[0] = JTAG_TMSSEL | (3 << 16) | 0x03; // send 4 bits: 1, 1, 0, 0 to get into ShiftIR state
	host[0] = JTAG_LAST   | (9 << 16) | inst; // send 10 bits of instruction data, we are now in Exit1-IR state
	host[0] = JTAG_TMSSEL | (1 << 16) | 0x01; // send 2 bits: 1, 0 on TMS to get back to idle state
}

void jtag_instruction_pause_ir(volatile uint32_t *host, uint32_t inst)
{
	host[0] = JTAG_TMSSEL | (3 << 16) | 0x03; // send 4 bits: 1, 1, 0, 0 to get into ShiftIR state
	host[0] = JTAG_LAST   | (9 << 16) | inst; // send 10 bits of instruction data, we are now in Exit1-IR state
	host[0] = JTAG_TMSSEL | (0 << 16) | 0x00; // send 1 bits: 0 on TMS to get to pause_ir state
}

void jtag_pause_ir_instruction(volatile uint32_t *host, uint32_t inst)
{
	host[0] = JTAG_TMSSEL | (5 << 16) | 0x0F; // send 6 bits: 1, 1, 1, 1, 0, 0 to get into ShiftIR state
	host[0] = JTAG_LAST   | (9 << 16) | inst; // send 10 bits of instruction data, we are now in Exit1-IR state
	host[0] = JTAG_TMSSEL | (0 << 16) | 0x00; // send 1 bits: 0 on TMS to get to pause_ir state
}

void jtag_exit_pause_ir(volatile uint32_t *host)
{
	host[0] = JTAG_TMSSEL | (2 << 16) | 0x03; // send 3 bits: 1, 1, 0 to get into idle state
}

void jtag_senddata(volatile uint32_t *host, uint8_t *data, int length, uint8_t *readback)
{
	host[0] = JTAG_TMSSEL | (2 << 16) | 0x01; // send 3 bits: 1, 0, 0 to get into ShiftDR state

	uint32_t address = (uint32_t)data;
	if ((address & 3) == 0) { // aligned
		while(length > 32) {
			host[1] = *((uint32_t *)data);
			data += 4;
			length -= 32;
		}
	}

	while(length > 0) {
		uint32_t cmd = (data) ? (uint32_t)*data : 0;
		if (length <= 8) {
			cmd |= JTAG_LAST;
			cmd |= ((uint32_t)(length - 1)) << 16; // up to 8 bits, last.
		} else {
			cmd |= (7 << 16); // 8 bits, not last
		}
		host[0] = cmd;
		if (readback) {
			uint32_t rb = host[0];
			*(readback++) = (uint8_t)rb;
		}
		length -= 8;
		if(data)
			data++;
	}
	host[0] = JTAG_TMSSEL | (1 << 16) | 0x01; // send 2 bits: 1, 0 on TMS to get back to idle state
}


void vji_read(JTAG_Access_t *controller, uint16_t addr, uint8_t *dest, int bytes)
{
	volatile uint32_t *host = controller->host;
	uint16_t address = controller->address + addr;
	jtag_instruction(host, 14); // user 1
	jtag_senddata(host, (uint8_t *)(&address), controller->drLength, NULL);
	jtag_instruction(host, 12); // user 0

	// send a string of zeros to read the data, but set the last bit to 1 to indicate we do not want to read the fifo anymore.
	// this bit is a don't care for all other chains.
	host[0] = JTAG_TMSSEL | (2 << 16) | 0x01; // send 3 bits: 1, 0, 0 to get into ShiftDR state
	for(int i=1;i<=bytes;i++) {
		uint32_t cmd = (i == bytes) ? (0x70080 | JTAG_LAST) : 0x70000;
		host[0] = cmd;
		uint32_t rb = host[0];
		*(dest++) = (uint8_t)rb;
	}
	host[0] = JTAG_TMSSEL | (1 << 16) | 0x01; // send 2 bits: 1, 0 on TMS to get back to idle state
}

void vji_read_fifo(JTAG_Access_t *controller, uint32_t *data, int words)
{
	volatile uint32_t *host = controller->host;
	uint16_t address = controller->address + 4;
	uint8_t *dest = (uint8_t *)data;
	jtag_instruction(host, 14); // user 1
	jtag_senddata(host, (uint8_t *)&address, controller->drLength, NULL);
	int iter = 0;

	while(words) {
		jtag_instruction(host, 12); // user 0
		iter++;

		// send a string of zeros to read the data, but set the last bit to 1 to indicate we do not want to read the fifo anymore.
		// this bit is a don't care for all other chains.
		host[0] = JTAG_TMSSEL | (2 << 16) | 0x01; // send 3 bits: 1, 0, 0 to get into ShiftDR state
		host[0] = 0x70000; // read a byte
		uint8_t rb = (uint8_t)host[0];
		int bytes = (rb & 0xFC);
		if (!bytes) {
			printf(" Reading fifo FAIL.\n");
			break;
		}
		for(int i=1;i<=bytes;i++) {
			uint32_t cmd = (i == bytes) ? (0x70080 | JTAG_LAST) : 0x70000;
			host[0] = cmd;
			uint32_t rb = host[0];
			*(dest++) = (uint8_t)rb;
		}
		words -= (bytes >> 2);
		host[0] = JTAG_TMSSEL | (1 << 16) | 0x01; // send 2 bits: 1, 0 on TMS to get back to idle state
	}
}

void vji_write(JTAG_Access_t *controller, uint16_t addr, uint8_t *data, int bytes)
{
	volatile uint32_t *host = controller->host;
	uint16_t address = controller->address + addr;
	jtag_instruction(host, 14); // user 1
	jtag_senddata(host, (uint8_t *)&address, controller->drLength, NULL);
	jtag_instruction(host, 12); // user 0
	jtag_senddata(host, data, 8*bytes, NULL);
}

void vji_write_block(JTAG_Access_t *controller, uint32_t *data, int words)
{
	volatile uint32_t *host = controller->host;
	uint16_t address = controller->address + 6;
	jtag_instruction(host, 14); // user 1
	jtag_senddata(host, (uint8_t *)&address, controller->drLength, NULL);
	jtag_instruction(host, 12); // user 0

	host[0] = JTAG_TMSSEL | (2 << 16) | 0x01; // send 3 bits: 1, 0, 0 to get into ShiftDR state
	for(int i=1;i < words;i++) {
		host[1] = *(data++);
	}
	host[0] = 0xF0000 | ((*data) & 0xFFFF);
	host[0] = JTAG_LAST | 0xF0000 | ((*data) >> 16);
	host[0] = JTAG_TMSSEL | (1 << 16) | 0x01; // send 2 bits: 1, 0 on TMS to get back to idle state
}

void vji_read_memory(JTAG_Access_t *controller, uint32_t address, int words, uint32_t *dest)
{
/*
	if (words > 256) {
		printf("Reading more than 256 DWORDS is not supported.\n");
		return;
	}
*/
	while(words > 0) {
		int now = (words > 256) ? 256 : words;
		uint32_t caddr = address;
		uint8_t read_cmd[] = { 0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00, 0x07, 0x00, 0x03 };
		read_cmd[0] = (uint8_t)caddr;  caddr >>= 8;
		read_cmd[2] = (uint8_t)caddr;  caddr >>= 8;
		read_cmd[4] = (uint8_t)caddr;  caddr >>= 8;
		read_cmd[6] = (uint8_t)caddr;
		read_cmd[8] = (uint8_t)(now - 1);
		vji_write(controller, 5, read_cmd, 10);
		vji_read_fifo(controller, dest, now);
		address += 4*now;
		words -= now;
		dest += now;
	}
}

void vji_write_memory(JTAG_Access_t *controller, uint32_t address, int words, uint32_t *src)
{
	uint8_t write_cmd[] = { 0x20, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00, 0x07, 0x80, 0x01 };
	write_cmd[0] = (uint8_t)address;  address >>= 8;
	write_cmd[2] = (uint8_t)address;  address >>= 8;
	write_cmd[4] = (uint8_t)address;  address >>= 8;
	write_cmd[6] = (uint8_t)address;
	vji_write(controller, 5, write_cmd, 10);
	vji_write_block(controller, src, words);
}

void jtag_clear_fpga(volatile uint32_t *host)
{
	jtag_reset_to_idle(host);
	host[0] = JTAG_TMSSEL | (3 << 16) | 0x03; // send 4 bits: 1, 1, 0, 0 to get into ShiftIR state
	host[0] = JTAG_LAST   | (9 << 16) | 2;    // send 10 bits of instruction data, we are now in Exit1-IR state
	host[0] = JTAG_TMSSEL | (3 << 16) | 0x06; // send 4 bits: 0, 1, 1, 0 to get into Idle state through Pause-IR
	host[0] = JTAG_TMSSEL |	(12499 << 16);    // send 12500 zero bits on TDI, stay in same state
	host[0] = JTAG_TMSSEL |	(12499 << 16);    // send 12500 zero bits on TDI, stay in same state
}

void jtag_configure_fpga(volatile uint32_t *host, uint8_t *data, int length)
{
/*	ENDDR IDLE;
	ENDIR IRPAUSE;
	STATE IDLE;
	SIR 10 TDI (002);
	RUNTEST IDLE 25000 TCK ENDSTATE IDLE;
	SDR 5748760 TDI (00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFBF59000000000000000000000000000000000800000000000000000000000000000000000000000000000000000000000000000000000000);
	SIR 10 TDI (004);
	RUNTEST 125 TCK;
	SDR 732 TDI (000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000) TDO (00000000000000000000000000000000000000000000000000000
		0000000000000000000000000000000000000000000000000000000000400000000000000000000000000000000000000000000000000000000000000000000000) MASK (000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000400000
		000000000000000000000000000000000000000000000000000000000000000000);
	SIR 10 TDI (003);
	RUNTEST 102400 TCK;
	RUNTEST 512 TCK;
	SIR 10 TDI (3FF);
	RUNTEST 25000 TCK;
	STATE IDLE;
	*/
	jtag_reset_to_idle(host);
	jtag_instruction(host, 0x2D0); // Disengage active?
	host[0] = JTAG_TMSSEL | (3 << 16) | 0x03; // send 4 bits: 1, 1, 0, 0 to get into ShiftIR state
	host[0] = JTAG_LAST   | (9 << 16) | 2;    // send 10 bits of instruction data, we are now in Exit1-IR state
	host[0] = JTAG_TMSSEL | (3 << 16) | 0x06; // send 4 bits: 0, 1, 1, 0 to get into Idle state through Pause-IR
	host[0] = JTAG_TMSSEL |	(12499 << 16);    // send 12500 zero bits on TDI, stay in same state
	host[0] = JTAG_TMSSEL |	(12499 << 16);    // send 12500 zero bits on TDI, stay in same state
	jtag_senddata(host, data, 8*length, NULL);  // this brings us back to idle
	host[0] = JTAG_TMSSEL | (3 << 16) | 0x03; // send 4 bits: 1, 1, 0, 0 to get into ShiftIR state
	host[0] = JTAG_LAST   | (9 << 16) | 4;    // send 10 bits of instruction data, we are now in Exit1-IR state
	host[0] = JTAG_TMSSEL | (0 << 16) | 0x00; // send 1 bits: 0 on TMS to get to PauseIR
	host[0] = JTAG_TMSSEL | (2 << 16) | 0X03; // send 3 bits: 1, 1, 0 to get into idle ###
	host[0] = JTAG_TMSSEL | (124 << 16);      // send 125 clocks on TCK
	host[0] = JTAG_TMSSEL | (2 << 16) | 0x01; // send 3 bits: 1, 1, 0 to get into ShiftDR state.
	host[0] = JTAG_LAST   | (731 << 16);      // send 732 zero bits on TDI and exit to Exit1-DR
	host[0] = JTAG_TMSSEL | (1 << 16) | 0x01; // send 2 bits: 1, 0 on TMS to get back to idle state
	jtag_instruction_pause_ir(host, 3);
	host[0] = (16383 << 16); 				  // send 16384 clocks, but stay in idle
	host[0] = (16383 << 16); 				  // send 16384 clocks, but stay in idle
	host[0] = (16383 << 16); 				  // send 16384 clocks, but stay in idle
	host[0] = (16383 << 16); 				  // send 16384 clocks, but stay in idle
	host[0] = (16383 << 16); 				  // send 16384 clocks, but stay in idle

	host[0] = (16383 << 16); 				  // send 16384 clocks, but stay in idle
	host[0] = (16383 << 16); 				  // send 16384 clocks, but stay in idle
	host[0] = (16383 << 16); 				  // send 16384 clocks, but stay in idle

	host[0] = (16383 << 16); 				  // send 16384 clocks, but stay in idle
	host[0] = (4607  << 16);                  // send the remaining 4608 clocks, stay in idle
	jtag_pause_ir_instruction(host, 0x3FF);
	host[0] = (12499 << 16);                  // send 12500 zero bits on TDI, stay in same state
	host[0] = (12499 << 16);                  // send 12500 zero bits on TDI, stay in same state
	host[0] = (12499 << 16);                  // send 12500 zero bits on TDI, stay in same state
	jtag_exit_pause_ir(host);
	//jtag_reset_to_idle(host);
	volatile uint32_t read = host[0];         // this read forces us to wait until the jtag controller is done
//	printf("Configuring FPGA completed. %d\n");
}


int FindJtagAccess(volatile uint32_t *host, JTAG_Access_t *access)
{
	access->valid = 0;

	uint8_t hub_info[8] = {0, 0, 0, 0, 0, 0, 0, 0 };
	uint8_t hub_data;

	jtag_instruction(host, 14); // user 1
	jtag_senddata(host, hub_info, 64, NULL);
	jtag_instruction(host, 12); // user 0
	uint32_t info = 0;
	for(int i=0; i<8; i++) {
		info >>= 4;
		jtag_senddata(host, hub_info, 4, &hub_data);
		info |= ((uint32_t)hub_data) << 28;
	}
	//printf("Hub: %08X\n", info);

	if (info == 0xFFFFFFFF) {
		printf("No Hub found.\n");
		return -1;
	}

/*
	printf("  m   = %d\n", info & 255);
	printf("  mfg = %03x\n", (info & 0x7ff00) >> 8);
	printf("  N   = %d\n", (info & 0x7f80000) >> 19);
	printf("  ver = %02X\n", (info >> 27));
*/
	int N = (info & 0x7f80000) >> 19;
	int m = info & 255;
	int vji = -1;
	for(int j=1; j<=N; j++) {
		info = 0;
		for(int i=0; i<8; i++) {
			info >>= 4;
			jtag_senddata(host, hub_info, 4, &hub_data);
			//printf(".%2x", hub_data);
			info |= ((uint32_t)hub_data) << 28;
		}
		uint32_t node_id = (info & 0x7f80000) >> 19;
/*
		printf("Node %d: %08X\n", j, info);
		printf("  inst ID = %02x\n", info & 255);
		printf("  mfg     = %03x\n", (info & 0x7ff00) >> 8);
		printf("  node ID = %02x\n", node_id);
		printf("  version = %03X\n", (info >> 27));
*/
		if (node_id == 8) {
			vji = j;
		}
	}
	int bits = 0;
	while(N) {
		bits++;
		N >>= 1;
	}
	bits += m;
	uint8_t addr = vji << m;
	if (vji > 0) {
		printf("Virtual JTAG interface found on address %d. Addr = %02x. DR length = %d\n", vji, addr, bits);
	} else {
		printf("No virtual JTAG interface found.\n");
		return -2;
	}

	access->address = addr;
	access->drLength = bits;
	access->host = host;
	access->valid = 1;

	return 0;
}


