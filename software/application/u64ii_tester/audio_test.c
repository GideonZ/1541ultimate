/*
 * audio_test.c
 *
 *  Created on: Nov 13, 2016
 *      Author: gideon
 */

#include "FreeRTOS.h"
#include "audio_dma.h"
#include "fix_fft.h"
#include "task.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "u64ii_tester.h"

extern char _sine_stereo_start;

int doFFT(uint32_t *buffer, int *result, const int peak, int report)
{
    int errors = 0;
    short real[512];
    short imag[512];
    int maxampl = 0;
    for (int i = 0; i < 512; i++) {
        real[i] = (short)(buffer[2 * i] >> 16);
        if (abs(real[i]) > maxampl) {
            maxampl = abs(real[i]);
        }
        imag[i] = 0;
    }
    fix_fft(real, imag, 9, 0);
    int peakval = 0, maxpos = 0;
    int peakoutside = 0;
    for (int i = 0; i < 256; i++) {
        result[i] = abs((int)real[i] * (int)real[i] + (int)imag[i] * (int)imag[i]);
        // Outside of expected peak, should be zero, or close to zero.
        if ((i != peak) && (i != 0)) {
            if (result[i] > peakoutside) {
                peakoutside = result[i];
            }
            if (result[i] > 1000) {
                errors++;
            }
        }
        if (result[i] > peakval) {
            peakval = result[i];
            maxpos = i;
        }
        // printf("\n");
    }
    if (result[peak] < 30000000) {
        errors++;
    }
    printf("Peak detected at position %d (should be %d), level: %d.\n", maxpos, peak, peakval);
    printf("  Other max: %d, Input amplitude: %d\n", peakoutside, maxampl);

    if (errors && report) {
        printf("Audio test, errors = %d. Peak @ %d (%d)\n", errors, maxpos, peakval);

        for (int i = 0; i < 512; i++) {
            real[i] = (short)(buffer[2 * i] >> 16);
            printf("%d\n", real[i]);
        }
        printf("--------------------\n");
        for (int i = 0; i < 256; i++) {
            printf("%d\n", result[i]);
        }
    }

    return errors;
}


int TestAudio(int peakLeft, int peakRight, int report)
{
    int left = 0;
    int right = 0;

    uint32_t *buffer = malloc(5000); // that is 1250 samples (left and right combined, thus 625 each)
    play_audio((audio_dma_t *)U64TESTER_AUDIO_BASE, (int *)&_sine_stereo_start, 512*2, 1, e_32bit_stereo);
    vTaskDelay(100);
    record_audio((audio_dma_t *)U64TESTER_AUDIO_BASE, (int *)buffer, 1250, e_32bit_stereo);
    vTaskDelay(100);
    stop_audio((audio_dma_t *)U64TESTER_AUDIO_BASE);
    for (int x = 0; x < 1; x++) {
        int result[256];

        left += doFFT(&buffer[81], result, peakLeft, report);
        right += doFFT(&buffer[80], result, peakRight, report);
    }
    printf("Audiotest Failures Left / Right: %d / %d\n", left, right);
    free(buffer);
    return left + right;
}

int testDutSpeaker(void)
{
    return 0;
}
