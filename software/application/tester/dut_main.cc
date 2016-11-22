/*
 * dut_main.c
 *
 *  Created on: Nov 14, 2016
 *      Author: gideon
 */

#include "FreeRTOS.h"
#include "task.h"
#include "altera_avalon_pio_regs.h"
#include "altera_msgdma.h"
#include "system.h"

#include "stdio.h"
#include "stdlib.h"
#include "itu.h"
#include "u2p.h"
#include "usb_nano.h"
#include "dump_hex.h"
#include "i2c.h"
#include "rtc_only.h"

extern "C" {
#include "flash_switch.h"
#include "ethernet_test.h"
#include "audio_test.h"
#include "flash_programmer.h"
int checkUsbHub();
int checkUsbSticks();
void initializeUsb(void);
void codec_init(void);
}

#define BUFFER_LOCATION  (*(volatile uint32_t *)(0x20000784))
#define BUFFER_SIZE      (*(volatile uint32_t *)(0x20000788))
#define BUFFER_HEAD		 (*(volatile uint32_t *)(0x2000078C))
#define BUFFER_TAIL		 (*(volatile uint32_t *)(0x20000790))
#define DUT_TO_TESTER    (*(volatile uint32_t *)(0x20000794))
#define TESTER_TO_DUT	 (*(volatile uint32_t *)(0x20000798))
#define TEST_STATUS		 (*(volatile int *)(0x2000079C))
#define PROGRAM_DATALOC  (*(volatile uint32_t *)(0x200007A0))
#define PROGRAM_DATALEN  (*(volatile int *)(0x200007A4))
#define PROGRAM_ADDR     (*(volatile uint32_t *)(0x200007A8))
#define TIME             ((volatile uint8_t *)(0x200007B0)) // b0-bf
#define SIZE_OF_BUFFER   8192

extern uint8_t _nano_phytest_b_start;
extern uint32_t _nano_phytest_b_size;

int getNetworkPacket(uint8_t **payload, int *length);


void outbyte_buffer(int c)
{
	uint32_t next = (BUFFER_HEAD == (SIZE_OF_BUFFER-1)) ? 0 : BUFFER_HEAD+1;
	while(next == BUFFER_TAIL)
		;
	uint8_t *buffer = (uint8_t *)BUFFER_LOCATION;
	buffer[BUFFER_HEAD] = (uint8_t)c;
	BUFFER_HEAD = next;
}

extern uint32_t _sine_mono_start;

int generate_mono()
{
	alt_msgdma_dev *dma = alt_msgdma_open("/dev/audio_out_dma_csr");
	if (!dma) {
		printf("Cannot find DMA unit to play audio.\n");
		return -1;
	} else {
		const int samples = 256;
		alt_msgdma_standard_descriptor desc;
		desc.read_address = &_sine_mono_start;
		desc.transfer_length = samples * 4;
		desc.control = 0x80000400; // just the go bit and the park read bit for continuous output
		alt_msgdma_standard_descriptor_async_transfer(dma, &desc);
	}
	return 0;
}

int generate_stereo()
{
	return startAudioOutput(512);
}

int verify_stereo()
{
	return TestAudio(8, 12, 1);
}

int receiveEthernetPacket()
{
	uint8_t *packet;
	int length;
	if (getNetworkPacket(&packet, &length) == 0) {
		printf("No network packet received from tester.\n");
		return 1;
	} else {
		printf("YES! Network packet received with length = %d\n", length);
		if (length != 1000) {
			return 2;
		}
	}
	return 0;
}

int checkUsbPhy()
{
    // load the nano CPU code and start it.
	NANO_START = 0;
    int size = (int)&_nano_phytest_b_size;
    uint8_t *src = &_nano_phytest_b_start;
    uint16_t *dst = (uint16_t *)NANO_BASE;
    uint16_t temp;
    for(int i=0;i<size;i+=2) {
    	temp = (uint16_t)((*(src++)) << 8);
    	temp |= (uint16_t)(*(src++));
    	*(dst++) = temp;
	}
    for(int i=size;i<2048;i+=2) {
    	*(dst++) = 0;
    }
	NANO_START = 1;
	vTaskDelay(2);
	uint16_t expected[] = { 0x5A, 0xA5, 0x33, 0x99, 0x24, 0x04, 0x06, 0x00 };
	int errors = 0;
	volatile uint16_t *nano = (uint16_t *)(NANO_BASE + 0x7F0);
	for (int i=0;i<8;i++) {
		if(nano[i] != expected[i]) {
			printf("Expected %04x but got %04x\n", expected[i], nano[i]);
			errors++;
		}
	}
	return errors;
}

int programFlash()
{
	if (!PROGRAM_DATALOC) {
		printf("Memory Location of programming data not set.\n");
		return -7;
	}
	uint32_t *pul = (uint32_t *)PROGRAM_DATALOC;
	return doFlashProgram(PROGRAM_ADDR, pul, PROGRAM_DATALEN);
}

int testButtons()
{
	int start = (int)xTaskGetTickCount();
	int end = start + 2400; // 12 seconds

	int retval = 7;
	while ((end - (int)xTaskGetTickCount()) > 0) {
		uint32_t buttons = (IORD_ALTERA_AVALON_PIO_DATA(PIO_2_BASE) >> 16) & 7;
		switch(buttons) {
		case 1:
			retval &= ~1;
			break;
		case 2:
			retval &= ~2;
			break;
		case 3:
		case 5:
		case 6:
		case 7:
			retval |= 16;
			break;
		case 4:
			retval &= ~4;
			break;
		default:
			break;
		}
		if (retval == 0)
			break;
	}

	return retval;
}

int testRtcAccess(void)
{
	int test_c = 0;

	ENTER_SAFE_SECTION
	i2c_write_byte(0xA2, 0x03, 0x55);
	int res;
	uint8_t rambyte = i2c_read_byte(0xA2, 0x03, &res);
	LEAVE_SAFE_SECTION
	if (res)
		return -9;

	if (rambyte != 0x55)
		test_c ++;

	ENTER_SAFE_SECTION
	i2c_write_byte(0xA2, 0x03, 0xAA);
	rambyte = i2c_read_byte(0xA2, 0x03, &res);
	LEAVE_SAFE_SECTION
	if (res)
		return -9;
	if (rambyte != 0xAA)
		test_c ++;

	return test_c;
}

int readRtc()
{
	ENTER_SAFE_SECTION
	volatile uint8_t *pb = TIME;
	int res;
	for(int i=0;i<11;i++) {
        *(pb++) = i2c_read_byte(0xA2, i, &res);
        if (res) {
        	break;
        }
    }
	LEAVE_SAFE_SECTION
	return res;
}

int writeRtc()
{
	ENTER_SAFE_SECTION
	volatile uint8_t *pb = TIME;
	int result = 0;
	for(int i = 0; (i < 11) && (result == 0); i++) {
        result |= i2c_write_byte(0xA2, i, *(pb++));
    }
	LEAVE_SAFE_SECTION
	char date[32], time[32];
	printf("Written to RTC: %s ", rtc.get_long_date(date, 32));
	printf("%s\n", rtc.get_time_string(time, 32));

	return result;
}

extern "C" {
void main_task(void *context)
{
	// Initialize software fifo based character buffer
	uint8_t *buffer = (uint8_t *)malloc(SIZE_OF_BUFFER);
	BUFFER_SIZE = SIZE_OF_BUFFER;
	BUFFER_HEAD = 0;
	BUFFER_TAIL = 0;
	BUFFER_LOCATION = (uint32_t)buffer;
	custom_outbyte = outbyte_buffer;

	U2PIO_ULPI_RESET = 1;
	U2PIO_SPEAKER_EN = 1;
	U2PIO_ULPI_RESET = 0;

	codec_init();

	DUT_TO_TESTER = 1;
	TEST_STATUS = 0;
	IOWR_ALTERA_AVALON_PIO_DATA(PIO_3_BASE, 0); // LEDs off

	while (1) {
		vTaskDelay(1);
		if (!TESTER_TO_DUT)
			continue;

		int result = 0;

		switch (TESTER_TO_DUT) {
		case 1:
			result = generate_mono();
			break;
		case 2:
			result = generate_stereo();
			break;
		case 3:
			result = verify_stereo();
			break;
		case 4:
			result = testFlashSwitch();
			break;
		case 5:
			result = sendEthernetPacket(900);
			break;
		case 6:
			result = receiveEthernetPacket();
			break;
		case 7:
			result = checkUsbPhy();
			break;
		case 8:
			result = checkUsbHub();
			break;
		case 9:
			result = checkUsbSticks();
			break;
		case 10:
			result = testButtons();
			break;
		case 11:
			initializeUsb();
			break;
		case 12:
			result = programFlash();
			break;
		case 13:
			result = testRtcAccess();
			break;
		case 14:
			result = readRtc();
			break;
		case 15:
			result = writeRtc();
			break;
		case 16:
			result = protectFlashes();
			break;
		case 99:
			printf("Alive!\n");
			break;
		default:
			result = -5;
		}
		TEST_STATUS = result;
		TESTER_TO_DUT = 0;
		DUT_TO_TESTER ++;
	}
}
}
