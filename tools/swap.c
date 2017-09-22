/*
 * swap.c
 *
 *  Created on: Sep 11, 2016
 *      Author: gideon
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int usage()
{
    printf("Usage: swap [-h] <input file> <output file> [truncate to size]\n");
    return 0;
}

int main(int argc, char **argv)
{
	FILE *f;
    FILE *fo;
    unsigned char buffer[4096];
    char *filename, *filename2;
    int i, j, size;

    int argfail = 0;
    int argidx = 1;

    if (argc <= argidx) {
        return usage();
    }
    int headerStrip = 0;
    int nibbleShift = 0;
    if (strcmp(argv[argidx], "-h") == 0) {
    	headerStrip = 1;
    	argidx ++;
    } else if (strcmp(argv[argidx], "-n") == 0) {
    	headerStrip = 1;
    	nibbleShift = 1;
    	argidx ++;
    }

    if(nibbleShift)
    	printf("NibbleShift Enabled\n");
    if(headerStrip)
    	printf("HeaderStrip Enabled\n");


    if (argc <= argidx) {
        return usage();
    }
    filename = argv[argidx++];

    if (argc <= argidx) {
        return usage();
    }
    filename2 = argv[argidx++];

    int truncate = 0x7FFFFFFF;
    if (argc > argidx) {
    	truncate = (int)strtol(argv[argidx++], NULL, 16);
    }

    f = fopen(filename, "rb");
    if(!f) {
        printf("Can't open file %s.\n", filename);
        return 1;
    }

    fo = fopen(filename2, "wb");
    if(!fo) {
        printf("Can't open file %s.\n", filename2);
        fclose(f);
        return 1;
    }

	uint8_t swapped_bytes[256];
	for(i=0;i<256;i++) {
    	uint8_t temp = (uint8_t)i;
    	uint8_t swapped = 0;
    	for(j=0; j<8; j++) {
    		swapped <<= 1;
    		swapped |= (temp & 1);
    		temp >>= 1;
    	}
    	swapped_bytes[i] = swapped;
    }

    uint8_t nibble = 0x0F;
    uint8_t nibbleTemp;

	while(!feof(f) && (truncate)) {
		// Truncating
		int now = (truncate > 4096) ? 4096 : truncate;
		truncate -= now;
		int read = fread((char *)buffer, 1, now, f);

		// Header stripping
		int start = 0;
		if (headerStrip) {
			for (i=0;i<read;i++) {
				if (buffer[i] == 0xFF) {
					start = i;
					headerStrip = 0;
					truncate += start;
					break;
				}
			}
		}

		// Nibbleshift
		if (nibbleShift) {
			for(i=0;i<read;i++) {
				nibbleTemp = buffer[i] >> 4;
				buffer[i] = (buffer[i] << 4) | nibble;
				nibble = nibbleTemp;
			}
		}

		// Swapping
		for(i=0;i<read;i++) {
			buffer[i] = swapped_bytes[buffer[i]];
		}
		fwrite(&buffer[start], 1, read-start, fo);
    }

    fclose(f);
    fclose(fo);
    return 0;
}
