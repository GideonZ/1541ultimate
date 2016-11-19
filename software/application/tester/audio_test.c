/*
 * audio_test.c
 *
 *  Created on: Nov 13, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "fix_fft.h"
#include "altera_msgdma.h"
#include "altera_avalon_pio_regs.h"

#include "FreeRTOS.h"
#include "task.h"

extern char _sine_stereo_start;


int doFFT(uint32_t *buffer, int *result, const int peak, int report)
{
	int errors = 0;
	short real[512];
	short imag[512];
	for(int i=0; i < 512; i++) {
		real[i] = (short)((buffer[2*i] & 0x00FFFF00) >> 8);
		imag[i] = 0;
	}
	fix_fft(real, imag, 9, 0);
	int peakval = 0, maxpos = 0;

	for(int i=0;i<256;i++) {
		result[i] = abs((int)real[i] * (int)real[i] + (int)imag[i] * (int)imag[i]);
		if ((i != peak) && (i != 0) && (result[i] > 1000)) {
			errors ++;
		}
		if (result[i] > peakval) {
			peakval = result[i];
			maxpos = i;
		}
		//printf("\n");
	}
	if (result[peak] < 30000000) {
		errors ++;
	}
	// printf("Peak not detected at position %d. Peak @ %d (%d)\n", peak, maxpos, peakval);

	if (errors && report) {
		printf("Audio test, errors = %d. Peak @ %d (%d)\n", errors, maxpos, peakval);
/*
		for(int i=0; i < 512; i++) {
			real[i] = (short)((buffer[2*i] & 0x00FFFF00) >> 8);
			printf("%d\n", real[i]);
		}
		for(int i=0;i<256;i++) {
			printf("%ld\n", result[i]);
		}
*/
	}

	return errors;
}

int startAudioOutput(int samples)
{
	alt_msgdma_dev *dma = alt_msgdma_open("/dev/audio_out_dma_csr");
	if (!dma) {
		printf("Cannot find DMA unit to play audio.\n");
		return -1;
	}
	alt_msgdma_standard_descriptor desc;
	desc.read_address = (uint32_t *)&_sine_stereo_start;
	desc.transfer_length = samples * 8;
	desc.control = 0x80000400; // just the go bit and the park read bit for continuous output
	alt_msgdma_standard_descriptor_async_transfer(dma, &desc);
	vTaskDelay(50);
	return 0;
}

int stopAudioOutput(void)
{
	alt_msgdma_dev *dma = alt_msgdma_open("/dev/audio_out_dma_csr");
	if (!dma) {
		printf("Cannot find DMA unit to play audio.\n");
		return -1;
	}
	const int samples = 128;
	alt_msgdma_standard_descriptor desc;
	desc.read_address = (uint32_t *)&_sine_stereo_start;
	desc.transfer_length = samples * 8;
	desc.control = 0x80000000; // one last time
	alt_msgdma_standard_descriptor_async_transfer(dma, &desc);
	vTaskDelay(50);
	return 0;
}


int TestAudio(int peakLeft, int peakRight, int report)
{
	alt_msgdma_dev *dma_in = alt_msgdma_open("/dev/audio_in_dma_csr");
	if (!dma_in) {
		printf("Cannot find DMA unit to record audio.\n");
		return -2;
	}

	int left = 0;
	int right = 0;

	uint32_t *buffer = malloc(5000);
	alt_msgdma_standard_descriptor desc_in;
	desc_in.write_address = buffer;
	desc_in.transfer_length = 5000;
	desc_in.control = 0x80000000; // just once

	for(int x=0; x < 1; x++) {
		alt_msgdma_standard_descriptor_sync_transfer(dma_in, &desc_in);
		alt_msgdma_standard_descriptor_sync_transfer(dma_in, &desc_in);

		int result[256];

		if(buffer[80] & 0x1000000) {
			left += doFFT(&buffer[81], result, peakLeft, report);
			right += doFFT(&buffer[80], result, peakRight, report);
		} else {
			left += doFFT(&buffer[80], result, peakLeft, report);
			right += doFFT(&buffer[81], result, peakRight, report);
		}
	}
	printf("Audiotest Failures Left / Right: %d / %d\n", left, right);
	free(buffer);
	return left+right;
}

int testDutSpeaker(void)
{
	alt_msgdma_dev *dma_in = alt_msgdma_open("/dev/audio_in_dma_csr");
	if (!dma_in) {
		printf("Cannot find DMA unit to record audio.\n");
		return -2;
	}

	IOWR_ALTERA_AVALON_PIO_SET_BITS(PIO_1_BASE, (1 << 31));

	uint32_t *buffer = malloc(5000);
	alt_msgdma_standard_descriptor desc_in;
	desc_in.write_address = buffer;
	desc_in.transfer_length = 5000;
	desc_in.control = 0x80000000; // just once

	alt_msgdma_standard_descriptor_sync_transfer(dma_in, &desc_in);
	alt_msgdma_standard_descriptor_sync_transfer(dma_in, &desc_in);

	int result[256];
	int err = doFFT(buffer, result, 10, 0);
	free(buffer);
	printf("Audio speaker test. %d\n", err);
	return err;
}
