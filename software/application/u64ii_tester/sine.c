/*
 * sine.c
 *
 *  Created on: Nov 13, 2016
 *      Author: gideon
 */

#include <stdio.h>
#include <math.h>
#include <stdint.h>

int main(int argc, char **argv)
{
	// Generate 1.125 kHz (that is 42(2/3) samples per wave)
	// and for the second channel 750 Hz (that is 64 samples per channel)
	// in 128 samples, 3 waves of 1.125 kHz can fit and 2 waves of 750 Hz.

	int samples = 128;
	int32_t waves[2*samples];

	double step1 = (2.0 * 3.14159265359) / ( 512.0 / 8.0);
	double step2 = (2.0 * 3.14159265359) / ( 512.0 / 12.0);
	double ampl  = pow(2.0, 30.0);

	// FFT is in steps of 93.75 Hz.
	// 1125 Hz => peak at position 12.
	// 750 Hz => peak at position 8.

	for(int i=0; i < samples; i++) {
		waves[2*i]   = (int32_t)(ampl * sin(step1 * (float)i));
		waves[2*i+1] = (int32_t)(ampl * cos(step2 * (float)i));
	}

	FILE *fo = fopen("waves_8_12.bin", "wb");
	fwrite(waves, samples, 8, fo);
	fclose(fo);

	// Peak at 11 => 1031.25 Hz => 512 samples; step divider = 46.545454
	// Peak at 7  => 656.25 Hz  => 512 samples; step divider = 73.142857143
	step1 = (2.0 * 3.14159265359) / ( 512.0 / 11.0 );
	step2 = (2.0 * 3.14159265359) / ( 512.0 / 7.0 );
	samples = 512;
	uint32_t waves2[2*samples];

	for(int i=0; i < samples; i++) {
		waves2[2*i]   = (int32_t)(ampl * sin(step1 * (float)i));
		waves2[2*i+1] = (int32_t)(ampl * cos(step2 * (float)i));
	}

	fo = fopen("waves_11_7.bin", "wb");
	fwrite(waves2, samples, 8, fo);
	fclose(fo);

	return 0;
}
