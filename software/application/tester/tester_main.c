#include <stdio.h>
#include "system.h"
#include <stdint.h>
#include "altera_avalon_spi.h"
#include "altera_avalon_pio_regs.h"


void configure_adc(void)
{
	uint8_t reset = 0x10;
	uint8_t setup = 0x68; // 01101000 always on reference
	alt_avalon_spi_command(SPI_0_BASE, 0, 1, &reset, 0, NULL, 0);
	alt_avalon_spi_command(SPI_0_BASE, 0, 1, &setup, 0, NULL, 0);
}

void adc_read_all(uint8_t *buffer)
{
	uint8_t convert_all = 0xB8; // 1.0111.00.x
	alt_avalon_spi_command(SPI_0_BASE, 0, 1, &convert_all, 0, NULL, 0);
	while((IORD_ALTERA_AVALON_PIO_DATA(PIO_1_BASE) >> 12) & 1)
		;
	alt_avalon_spi_command(SPI_0_BASE, 0, 0, NULL, 16, buffer, 0);
	// AIN: isense vcc_half, v50_half, v43_half, v33, v25, v18, v12
}

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

void jtag_senddata(volatile uint32_t *host, uint8_t *data, int length, uint8_t *readback)
{
	host[0] = JTAG_TMSSEL | (2 << 16) | 0x01; // send 3 bits: 1, 0, 0 to get into ShiftDR state
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

void jtag_read_vji(volatile uint32_t *host, uint8_t *vji_inst, int drLength, uint8_t *dest, int bytes)
{
	jtag_instruction(host, 14); // user 1
	jtag_senddata(host, vji_inst, drLength, NULL);
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

void jtag_read_vji_fifo(volatile uint32_t *host, uint8_t *vji_inst, int drLength, uint32_t *data, int words)
{
	uint8_t *dest = (uint8_t *)data;
	jtag_instruction(host, 14); // user 1
	jtag_senddata(host, vji_inst, drLength, NULL);
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
			printf("Fail.\n");
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
	printf("Reading the fifo took %d iterations.\n", iter);
}

void jtag_write_vji(volatile uint32_t *host, uint8_t *vji_inst, int drLength, uint8_t *data, int bytes)
{
	jtag_instruction(host, 14); // user 1
	jtag_senddata(host, vji_inst, drLength, NULL);
	jtag_instruction(host, 12); // user 0
	jtag_senddata(host, data, 8*bytes, NULL);
}

void jtag_write_vji_block(volatile uint32_t *host, uint8_t *vji_inst, int drLength, uint32_t *data, int words)
{
	jtag_instruction(host, 14); // user 1
	jtag_senddata(host, vji_inst, drLength, NULL);
	jtag_instruction(host, 12); // user 0

	host[0] = JTAG_TMSSEL | (2 << 16) | 0x01; // send 3 bits: 1, 0, 0 to get into ShiftDR state
	for(int i=1;i < words;i++) {
		host[1] = *(data++);
		//host[0] = 0xF0000 | ((*(data++)) & 0xFFFF);
	}
	host[0] = 0xF0000 | ((*data) & 0xFFFF);
	host[0] = JTAG_LAST | 0xF0000 | ((*data) >> 16);
	host[0] = JTAG_TMSSEL | (1 << 16) | 0x01; // send 2 bits: 1, 0 on TMS to get back to idle state
}

extern unsigned char _dut_svf_start;
extern unsigned char _dut_svf_size;

void play_svf(char *text, volatile uint32_t *);

int main(int argc, char **argv)
{
	printf("Hello world.\n");
	uint8_t buffer[16];
	configure_adc();
	adc_read_all(buffer);

	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, (1 << 4));
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, (1 << 5));
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, (1 << 6));
	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, (1 << 7));

	for(int i=0;i<8;i++) {
		uint32_t value = ((uint32_t)buffer[2*i]) << 8;
		value |= ((uint32_t)buffer[2*i+1]);
		printf("%ld ", value);
	}
	printf("\n");

	//printf("SVF file @ %p (%d)\n", &_dut_svf_start, (int)&_dut_svf_size);
	play_svf((char *)(&_dut_svf_start), (volatile uint32_t *)JTAG_1_BASE);

	uint8_t idcode[4] = {0, 0, 0, 0};
	uint8_t hub_info[8] = {0, 0, 0, 0, 0, 0, 0, 0 };
	uint8_t hub_data;
	uint8_t vji_data[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	volatile uint32_t *jtag1 = (volatile uint32_t *)JTAG_1_BASE;
	jtag_reset_to_idle(jtag1);
	jtag_instruction(jtag1, 6);
	jtag_senddata(jtag1, idcode, 32, idcode);

	printf("ID code: %02x %02x %02x %02x\n", idcode[3], idcode[2], idcode[1], idcode[0]);

	jtag_instruction(jtag1, 14); // user 1
	jtag_senddata(jtag1, hub_info, 64, NULL);
	jtag_instruction(jtag1, 12); // user 0
	uint32_t info = 0;
	for(int i=0; i<8; i++) {
		info >>= 4;
		jtag_senddata(jtag1, hub_info, 4, &hub_data);
		info |= ((uint32_t)hub_data) << 28;
	}
	printf("Hub: %08X\n", info);
	printf("  m   = %d\n", info & 255);
	printf("  mfg = %03x\n", (info & 0x7ff00) >> 8);
	printf("  N   = %d\n", (info & 0x7f80000) >> 19);
	printf("  ver = %02X\n", (info >> 27));
	int N = (info & 0x7f80000) >> 19;
	int m = info & 255;
	int vji = -1;
	for(int j=1; j<=N; j++) {
		info = 0;
		for(int i=0; i<8; i++) {
			info >>= 4;
			jtag_senddata(jtag1, hub_info, 4, &hub_data);
			//printf(".%2x", hub_data);
			info |= ((uint32_t)hub_data) << 28;
		}
		uint32_t node_id = (info & 0x7f80000) >> 19;
		printf("Node %d: %08X\n", j, info);
		printf("  inst ID = %02x\n", info & 255);
		printf("  mfg     = %03x\n", (info & 0x7ff00) >> 8);
		printf("  node ID = %02x\n", node_id);
		printf("  version = %03X\n", (info >> 27));
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
	}

	uint8_t regAddr;

	regAddr = addr + 0;
	jtag_read_vji(jtag1, &regAddr, bits, vji_data, 8);
	for(int i=0;i<8;i++) {
		printf("%02x ", vji_data[i]);
	}
	printf("\n");

	regAddr = addr + 1;
	jtag_read_vji(jtag1, &regAddr, bits, vji_data, 8);
	for(int i=0;i<8;i++) {
		printf("%02x ", vji_data[i]);
	}
	printf("\n");

	regAddr = addr + 2;
	uint8_t led = 0x05;
	jtag_write_vji(jtag1, &regAddr, bits, &led, 1);

	regAddr = addr + 4;
	jtag_read_vji(jtag1, &regAddr, bits, vji_data, 8);
	for(int i=0;i<8;i++) {
		printf("%02x ", vji_data[i]);
	}
	printf("\n");

	uint32_t mem_block[64];
	//jtag_read_vji_fifo(jtag1, &regAddr, bits, mem_block, 64);

	for(int i=0;i<64;i++) {
		mem_block[i] = 0x01020304 * i;
	}
	mem_block[0] = 0xDEADBABE;

	uint8_t write_cmd[] = { 0x20, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00, 0x07, 0x80, 0x01, 0x11, 0, 0x22, 0, 0x33, 0, 0x44, 0 };
	regAddr = addr + 5;
	jtag_write_vji(jtag1, &regAddr, bits, write_cmd, 18);
	regAddr = addr + 6;
	jtag_write_vji_block(jtag1, &regAddr, bits, mem_block, 64);

	uint8_t read_cmd[] = { 0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00, 0x07, 0x3F, 0x03 };
	regAddr = addr + 5;
	jtag_write_vji(jtag1, &regAddr, bits, read_cmd, 10);

	regAddr = addr + 3;
	jtag_read_vji(jtag1, &regAddr, bits, vji_data, 5);
	for(int i=0;i<5;i++) {
		printf("%02x ", vji_data[i]);
	}
	printf("\n");


	regAddr = addr + 4;
	jtag_read_vji_fifo(jtag1, &regAddr, bits, mem_block, 64);
	for(int i=0;i<64;i++) {
		printf("%08x ", mem_block[i]);
		if ((i & 3) == 3)
			printf("\n");
	}

}
