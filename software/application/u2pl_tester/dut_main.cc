/*
 * dut_main.c
 *
 *  Created on: Nov 14, 2016
 *      Author: gideon
 */

#include "FreeRTOS.h"
#include "task.h"

#include "stdio.h"
#include "stdlib.h"
#include "itu.h"
#include "u2p.h"
#include "usb_nano.h"
#include "dump_hex.h"
#include "i2c_drv.h"
#include "rtc_only.h"
#include "usb_base.h"

extern "C" {
#include "ethernet_test.h"
#include "audio_test.h"
int checkUsbHub();
int checkUsbSticks();
void printUsbSticksFound(void);
void initializeUsb(void);
void codec_init(void);
}
int getNetworkPacket(uint8_t **payload, int *length);

#define DUT_TO_TESTER    (*(volatile uint32_t *)(0x0094))
#define TESTER_TO_DUT	 (*(volatile uint32_t *)(0x0098))
#define TEST_STATUS		 (*(volatile int *)(0x009C))
#define TIME             ((volatile uint8_t *)(0x00B0)) // b0-bf

extern uint8_t _nano_phytest_b_start;
extern uint32_t _nano_phytest_b_size;
extern uint32_t _sine_mono_start;

int generate_mono()
{
/*
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
*/
	return 0;
}

int generate_stereo()
{
	return 0;//startAudioOutput(512);
}

int verify_stereo()
{
	return 0;//TestAudio(8, 12, 1);
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
    	temp = (uint16_t)(*(src++));
        temp |= (uint16_t)((*(src++)) << 8);
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

int testButtons()
{
	int start = (int)xTaskGetTickCount();
	int end = start + 2400; // 12 seconds

	int retval = 7;
/* 	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_3_BASE, 0x0D);

	while ((end - (int)xTaskGetTickCount()) > 0) {
		uint32_t buttons = (IORD_ALTERA_AVALON_PIO_DATA(PIO_2_BASE) >> 16) & 7;
		switch(buttons) {
		case 1:
			retval &= ~1;
			IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_3_BASE, 0x01);
			break;
		case 2:
			retval &= ~2;
			IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_3_BASE, 0x04);
			break;
		case 3:
		case 5:
		case 6:
		case 7:
			retval |= 16;
			IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_3_BASE, 0x02);
			break;
		case 4:
			retval &= ~4;
			IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_3_BASE, 0x08);
			break;
		default:
			break;
		}
		if (retval == 0)
			break;
	}
	IOWR_ALTERA_AVALON_PIO_CLEAR_BITS(PIO_3_BASE, 0x0F);
 */
	return retval;
}

int testRtcAccess(void)
{
	int test_c = 0;

	I2C_Driver i2c;
	ENTER_SAFE_SECTION
	i2c.i2c_write_byte(0xA2, 0x03, 0x55);
	int res;
	uint8_t rambyte = i2c.i2c_read_byte(0xA2, 0x03, &res);
	LEAVE_SAFE_SECTION
	if (res)
		return -9;

	if (rambyte != 0x55)
		test_c ++;

	ENTER_SAFE_SECTION
	i2c.i2c_write_byte(0xA2, 0x03, 0xAA);
	rambyte = i2c.i2c_read_byte(0xA2, 0x03, &res);
	LEAVE_SAFE_SECTION
	if (res)
		return -9;
	if (rambyte != 0xAA)
		test_c ++;

	return test_c;
}

int readRtc()
{
	I2C_Driver i2c;

    ENTER_SAFE_SECTION
	volatile uint8_t *pb = TIME;
	int res;
	for(int i=0;i<11;i++) {
        *(pb++) = i2c.i2c_read_byte(0xA2, i, &res);
        if (res) {
        	break;
        }
    }
	LEAVE_SAFE_SECTION
	return res;
}

int writeRtc()
{
    I2C_Driver i2c;
	ENTER_SAFE_SECTION
	volatile uint8_t *pb = TIME;
	int result = 0;
	for(int i = 0; (i < 11) && (result == 0); i++) {
        result |= i2c.i2c_write_byte(0xA2, i, *(pb++));
    }
	LEAVE_SAFE_SECTION
	char date[32], time[32];
	printf("Written to RTC: %s ", rtc.get_long_date(date, 32));
	printf("%s\n", rtc.get_time_string(time, 32));

	return result;
}

extern "C" {
void ultimate_main(void *context)
{
/*
	U2PIO_ULPI_RESET = 1;
	U2PIO_SPEAKER_EN = 1;
	U2PIO_ULPI_RESET = 0;

	codec_init();
*/
	printf("DUT Main..\n");

	DUT_TO_TESTER = 1;
	TEST_STATUS = 0;
//	IOWR_ALTERA_AVALON_PIO_DATA(PIO_3_BASE, 0); // LEDs off

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
		case 13:
			result = testRtcAccess();
			break;
		case 14:
			result = readRtc();
			break;
		case 15:
			result = writeRtc();
			break;
		case 17:
			printUsbSticksFound();
			break;
		case 99:
			printf("Alive V1.3!\n");
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
