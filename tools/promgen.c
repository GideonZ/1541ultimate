/* promgen.c
 *
 * This file converts a Xilinx bitfile into a promfile. 
 *
 * Copyright 2009 by Gideon Zweijtzer
 * 
 * Some routines from this file have been taken from bitinfo.c, created by:
 *
 * Copyright 2001, 2002 by David Sullins
 *
 * Promgen is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2 of the License.
 * 
 * Promgen is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifndef uchar
#define uchar unsigned char
#endif

/* first 13 bytes of a bit file */
static uchar head13[] = {0, 9, 15, 240, 15, 240, 15, 240, 15, 240, 0, 0, 1};

/* readhead13
 *
 * Read the first 13 bytes of the bit file.  Discards the 13 bytes but
 * verifies that they are correct.
 *
 * Return -1 if an error occurs, 0 otherwise.
 */
int readhead13 (FILE *f)
{
	int t;
	uchar buf[13];

	/* read header */
	t = fread(buf, 1, 13, f);
	if (t != 13)
	{
		return -1;
	}
	
	/* verify header is correct */
	t = memcmp(buf, head13, 13);
	if (t)
	{
		return -1;
	}
	
	return 0;
}

/* readsecthead
 *
 * Read the header of a bit file section.  The section letter is placed in
 * section buffer "buf".
 *
 * Return -1 if an error occurs, '0' otherwise.
 */
int readsecthead(char *buf, FILE *f)
{
	int t;

	/* get section letter */
	t = fread(buf, 1, 1, f);
	if (t != 1)
	{
		return -1;
	}
	return 0;
} 

/* readstring
 *
 * Read a string from the bit file header. The string is placed in "buf".
 *
 * Return -1 if an error occurs, '0' otherwise.
 */
int readstring(char *buf, int bufsize, FILE *f)
{
	char lenbuf[2];
    int len, t;
    
	/* read length */
	t = fread(lenbuf, 1, 2, f);
	if (t != 2) 
	{
		return -1;
	}

	/* convert 2-byte length to an int */
	len = (((int)lenbuf[0]) <<8) | lenbuf[1];
	
    if(len > bufsize) {
        return -1;
    }
    t = fread(buf, 1, len, f);

	if ((t != len) || (buf[len-1] != 0))
	{
		return -1;
	}
    return 0;
}

/* readlength
 *
 * Read in the bitstream length.  The section letter "e" is discarded
 * and the length is returned.
 *
 * Return -1 if an error occurs, length otherwise.
 */
int readlength(FILE *f)
{
	char s = 0;
	uchar buf[4];
	int length;
	int t;
	
	/* get length */
	t = fread(buf, 1, 4, f);
	if (t != 4)
	{
		return -1;
	}
	
	/* convert 4-byte length to an int */
	length = (((int)buf[0]) <<24) | (((int)buf[1]) <<16) 
	         | (((int)buf[2]) <<8) | buf[3];
	
	return length;
}

/* readhead
 * 
 * Read the entire bit file header.  The file pointer will be advanced to
 * point to the beginning of the bitstream, and the length of the bitstream
 * will be returned.
 *
 * Return -1 if an error occurs, length of bitstream otherwise.
 */
int readhead(FILE *f)
{
#define BUFSIZE 500

	int t, len;
	uchar typ;
    uchar buffer[BUFSIZE];
    	
	/* get first 13 bytes */
	t = readhead13(f);
	if (t) return t;
	
	/* get other fields */
    do {
    	if(readsecthead(&typ, f) == -1)
    	    return -1;

        switch(typ) {
            case 0x61:
            case 0x62:
            case 0x63:
            case 0x64:
    	        t = readstring(buffer, BUFSIZE, f);
            	if (t) return -1;
                break;
            case 0x65:
                len = readlength(f);
                break;
            default:
                printf("Unknown header field '%c'.\n", typ);
                return -1;
        }
        switch(typ) {
            case 0x61:
                printf("Design name:    %s\n", buffer);
                break;
            case 0x62:
                printf("Target device:  xc%s\n", buffer);
                break;
            case 0x63:
                printf("Bitfile date:   %s\n", buffer);
                break;
            case 0x64:
                printf("Bitfile time:   %s\n", buffer);
                break;
            case 0x65:
                printf("Bitfile length: %08x\n", len);
                return len;
        }
        
    }
    while(1);
    return -1;
}


int main(int argc, char **argv)
{
	unsigned char rotated[256];
	unsigned char buffer[1024];
    unsigned char r, t, b;
    
	int length, i, bytesread;

    FILE *fi, *fo;

    if(argc < 3) {
        printf("Usage: promgen [-r] <infile> <outfile>\n");
        exit(1);
    }

    int rotate = 1;
    int ar = 1;
    if(strcmp(argv[1], "-r")==0) {
        rotate = 0;
        ar++;
    }
    
    fi = fopen(argv[ar++], "rb");
    if(!fi) {
	    printf("Couldn't open file '%s' for reading.\n", argv[1]);
	    exit(2);
	}

    fo = fopen(argv[ar++], "wb");
    if(!fo) {
	    printf("Couldn't open file '%s' for writing.\n", argv[2]);
        fclose(fi);
	    exit(3);
	}

    // create rotate table
    for(i=0;i<256;i++) {
        r = 0;
        t = (unsigned char) i;
        for(b=0;b<8;b++) {
            r <<= 1;
            r |= (t & 1);
            t >>= 1;
        }
        rotated[i] = r;
    }

    // parse header of bitfile
    if((length = readhead(fi)) < 0) {
        printf("Parsing of header unsuccessful. Invalid bitfile?\n");
        exit(11);        
    }

    do {
        bytesread = fread(buffer, 1, 1024, fi);
        if(rotate) {
            for(i=0;i<bytesread;i++) {
                buffer[i] = rotated[buffer[i]];
            }
        }
        fwrite(buffer, 1, bytesread, fo);
        length -= bytesread;
    } while(length && bytesread);

    fclose(fo);
    fclose(fi);
    
    return 0;
}
