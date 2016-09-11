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
    printf("Usage: swap <input file> <output file>\n");
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

    // second optional switch
    if (argc <= argidx) {
        return usage();
    }
    filename = argv[argidx++];

    if (argc <= argidx) {
        return usage();
    }
    filename2 = argv[argidx++];


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

	while(!feof(f)) {
		int read = fread((char *)buffer, 1, 4096, f);
		for(i=0;i<read;i++) {
			buffer[i] = swapped_bytes[buffer[i]];
		}
		fwrite((char *)buffer, 1, read, fo);
    }

    fclose(f);
    fclose(fo);
    return 0;
}
