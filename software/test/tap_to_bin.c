/*
 * tap_to_bin.c
 *
 *  Created on: Apr 17, 2016
 *      Author: Gideon
 */
#include <stdio.h>
#include <stdint.h>

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	if (!f)
		return -1;

	uint8_t byte, n[3];

	while(!feof(f)) {
		fread(&byte, 1, 1, f);
		int p;
		if (byte) {
			p = 8 * (int)byte;
		} else {
			fread(n, 1, 3, f);
			p = (int)n[0] + (((int)n[1]) << 8) + (((int)n[2]) << 16);
		}
		printf("%d\n", p);
	}
	fclose(f);
}

