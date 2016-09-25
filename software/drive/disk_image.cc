/*
 * disk_image.cc
 *
 *  Created on: Apr 14, 2010
 *      Author: Gideon
 */
#include "disk_image.h"
#include "file_system.h"
#include "filemanager.h"

#include <ctype.h>
#include <string.h>
#include <unistd.h>

extern "C" {
	#include "dump_hex.h"
}
#include "userinterface.h" // for showing status information only

__inline uint32_t le_to_cpu_32(uint32_t a)
{
#ifdef NIOS
	return a;
#else
	uint32_t m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
#endif
}

#define cpu_to_le_32 le_to_cpu_32


extern uint8_t bam_header[];

#define HARDWARE_ENCODING 1

// Single nibble GCR table
const uint8_t gcr_table[] = { 0x0A, 0x0B, 0x12, 0x13, 0x0E, 0x0F, 0x16, 0x17,
                           0x09, 0x19, 0x1A, 0x1B, 0x0D, 0x1D, 0x1E, 0x15 };

const int track_lengths[] =      { 0x1E00, 0x1BE0, 0x1A00, 0x1860, 0x1E00, 0x1BE0, 0x1A00, 0x1860 };
const int sectors_per_track [] = { 21, 19, 18, 17, 21, 19, 18, 17 };
const int region_end[] =         { 17, 24, 30, 35, 52, 59, 65, 70 };
const int sector_gap_lengths[] = {  9, 19, 13, 10,  9, 19, 13, 10 };

#if HARDWARE_ENCODING == 0
static uint8_t gcr_table_hi_0[256];
static uint8_t gcr_table_lo_0[256];
static uint8_t gcr_table_hi_2[256];
static uint8_t gcr_table_lo_2[256];
static uint8_t gcr_table_hi_4[256];
static uint8_t gcr_table_lo_4[256];
static uint8_t gcr_table_hi_6[256];
static uint8_t gcr_table_lo_6[256];
static bool gcr_table_initialized = false;
#endif

int track_to_region(int track)
{
    for(int i=0;i<4;i++) {
        if(track < region_end[i])
            return i;
    }
    return 3; // illegal
}

int total_sectors_before_track(int track)
{
int count = 0;	
	for(int i=0;i<track;i++) {
		int region = track_to_region(i);
		count += sectors_per_track[region];		
	}
	return count;
}

GcrImage :: GcrImage(void)
{
#if HARDWARE_ENCODING == 0
    WORD gcr;
    if(!(gcr_table_initialized)) {
        for(int i=0;i<256;i++) {
            gcr = gcr_table[i & 15] | (WORD(gcr_table[i >> 4]) << 5);
            gcr_table_hi_0[i] = uint8_t(gcr >> 8);
            gcr_table_lo_0[i] = uint8_t(gcr & 0xFF);
            gcr <<= 2;
            gcr_table_hi_2[i] = uint8_t(gcr >> 8);
            gcr_table_lo_2[i] = uint8_t(gcr & 0xFF);
            gcr <<= 2;
            gcr_table_hi_4[i] = uint8_t(gcr >> 8);
            gcr_table_lo_4[i] = uint8_t(gcr & 0xFF);
            gcr <<= 2;
            gcr_table_hi_6[i] = uint8_t(gcr >> 8);
            gcr_table_lo_6[i] = uint8_t(gcr & 0xFF);
        }
        gcr_table_initialized = true;
    }
#endif
    gcr_image = gcr_data; // point to my own array
//    mounted_on = NULL;

	dummy_track = &gcr_data[C1541_MAX_GCR_LEN - 0x2000]; // drive logic can only address 8K
	memset(gcr_data, 0x55, C1541_MAX_GCR_LEN);
    invalidate();
}

//extern BYTE dummy_track[]; // dirty, but oh well.

void GcrImage :: invalidate(void)
{
    for(int i=0;i<C1541_MAXTRACKS;i++) {
    	track_address[i] = dummy_track;
    	track_length[i] = C1541_MAX_GCR_LEN;
    }
}

void GcrImage :: blank(void)
{
	memset(gcr_data, 0x00, C1541_MAX_GCR_LEN);

    uint8_t *gcr = gcr_image; // internal storage
    int length;
    
    for(int i=0;i<C1541_MAXTRACKS;i+=2) {
        length = track_lengths[track_to_region(i/2)];
        printf("Length of track %d is %d.\n", i+1, length);
        track_address[i] = gcr;
        track_length[i] = length;
		track_address[i + 1] = dummy_track;
        track_length[i + 1] = length;
        gcr += length;
    }
}
    
GcrImage :: ~GcrImage(void)
{
//    if(mounted_on)
//        mounted_on->remove_disk();
}

// aaaabbbb ccccdddd eeeeffff gggghhhh
// AAAAABBB BBCCCCCD DDDDEEEE EFFFFFGG GGGHHHHH
// ab => 0/1, shift = 6;
// cd => 1/2, shift = 4;
// ef => 2/3, shift = 2;
// gh => 3/4, shift = 0;

uint8_t *GcrImage :: convert_block_bin2gcr(uint8_t *bin, uint8_t *gcr, int len)
{
    uint32_t *dw = (uint32_t *)bin;
	for(int i=0;i<len;i+=4) {
#if HARDWARE_ENCODING > 0
    	GCR_ENCODER_BIN_IN_32 = *(dw++);
		*(gcr++) = GCR_ENCODER_GCR_OUT0;
    	*(gcr++) = GCR_ENCODER_GCR_OUT1;
    	*(gcr++) = GCR_ENCODER_GCR_OUT2;
    	*(gcr++) = GCR_ENCODER_GCR_OUT3;
    	*(gcr++) = GCR_ENCODER_GCR_OUT4;
#else
   	   *(gcr++) = gcr_table_hi_6[bin[i+0]];
       *(gcr++) = gcr_table_lo_6[bin[i+0]] | gcr_table_hi_4[bin[i+1]];
       *(gcr++) = gcr_table_lo_4[bin[i+1]] | gcr_table_hi_2[bin[i+2]];
       *(gcr++) = gcr_table_lo_2[bin[i+2]] | gcr_table_hi_0[bin[i+3]];
       *(gcr++) = gcr_table_lo_0[bin[i+3]];
#endif
    }
    return gcr;
}

uint8_t *GcrImage :: convert_track_bin2gcr(int track, uint8_t *bin, uint8_t *gcr, uint8_t *errors, int errors_size)
{
	int track_errors_index = total_sectors_before_track(track);	
	uint8_t errorcode;

	int region = track_to_region(track);
	
    uint8_t *bp, chk, b;
    uint8_t *end = gcr + track_lengths[region];

    for(int s=0;s<sectors_per_track[region];s++) {
		sector_buffer[0] = 7;
		sector_buffer[258] = 0;
		sector_buffer[259] = 0;

		header[0] = 8;
		header[3] = (uint8_t)track+1;
		header[4] = id2;
		header[5] = id1;
		header[6] = 0x0f;
		header[7] = 0x0f;
		errorcode = 0;
		if (errors != NULL && (track_errors_index + s) < errors_size){
			errorcode = errors[track_errors_index + s];
		}
        header[2] = (uint8_t)s;
        // calculate header checksum
        header[1] = header[2] ^ header[3] ^ header[4] ^ header[5];

		switch (errorcode){
			case 2:
				header[0] = 0;
				break;
			case 4:
				sector_buffer[0] = 0;
				break;
			case 9:
				header[1] = ~header[1];
				break;
			case 11:
				header[4] = (id2 ^ ~(s+0x55)) & 0xFF;
				header[5] = (id1 ^ ~(s+0xAA)) & 0xFF;
				break;
		}		
		if (errorcode != 3) {
			// put 5 sync bytes
			for(int i=0;i<5;i++)
				*(gcr++) = 0xFF;
		}
		else{
			for(int i=0;i<5;i++)
				*(gcr++) = 0x00;
		}
        gcr = convert_block_bin2gcr(header, gcr, 8);

        // put 9 gap bytes
        for(int i=0;i<9;i++)
            *(gcr++) = 0x55;

		if (errorcode != 3) {
			// put 5 sync bytes
			for(int i=0;i<5;i++)
				*(gcr++) = 0xFF;
		}
		else {
			for(int i=0;i<5;i++)
				*(gcr++) = 0x00;
		}
        // encapsulate sector with '7' byte as header and checksum at the end
        bp = &sector_buffer[1];
        chk = 0;
        for(int i=0;i<256;i++) {
            b = *(bin++);
            chk ^= b;
            *(bp++) = b;
        }
		if (errorcode != 5) {
			*(bp) = chk;
		}
		else {
			*(bp) = ~chk;
		}

        // insert (converted) sector
        gcr = convert_block_bin2gcr(sector_buffer, gcr, 260);

        for(int i=0;i<sector_gap_lengths[region];i++)
            *(gcr++) = 0x55;
    }

    // fill up to the end with gap
    while(gcr < end)
        *(gcr++) = 0x55;

    return gcr;
}

uint8_t *GcrImage :: find_sync(uint8_t *gcr_data, uint8_t *begin, uint8_t *end)
{
    bool wrap = false;
    int sync_count = 0;

    do {
        if(*gcr_data == 0xFF) {
            sync_count++;
        } else {
            if(sync_count > 2)
                return gcr_data; // byte after sync
            sync_count = 0;
        }
		if(gcr_data < end) {
    		gcr_data++;
        } else {
            if(wrap)
                return NULL;
            wrap = true;
            gcr_data = begin;
        }
    } while(1);
}

inline void conv_5bytes_gcr2bin(uint8_t **gcr, uint8_t *bin)
{
	uint8_t *b = *gcr;
//	printf("[%p: %b %b %b %b %b]\n", b, b[0], b[1], b[2], b[3], b[4]);
	GCR_DECODER_GCR_IN = *(b++);
	GCR_DECODER_GCR_IN = *(b++);
	GCR_DECODER_GCR_IN = *(b++);
	GCR_DECODER_GCR_IN = *(b++);
	GCR_DECODER_GCR_IN = *(b++);
	*(bin++) = GCR_DECODER_BIN_OUT0;
	*(bin++) = GCR_DECODER_BIN_OUT1;
	*(bin++) = GCR_DECODER_BIN_OUT2;
	*(bin++) = GCR_DECODER_BIN_OUT3;
	*gcr = b;
}

uint8_t *GcrImage :: wrap(uint8_t **current, uint8_t *begin, uint8_t *end, int count)
{
    uint8_t *gcr = *current;
//    printf("[%p-%p-%p:%d]", begin, gcr, end, count);
    if(gcr > (end - count)) {
//		printf("Wrap(%d)..", count);
		uint8_t *d = sector_buffer;
		uint8_t *s = gcr;
        *current = (gcr + count) - (end - begin);
		while((s < end)&&(count--))
			*(d++) = *(s++);
		s = begin;
		while(count--)
			*(d++) = *(s++);
		return sector_buffer; // set decode pointer to the scratch pad
	}
    *current = gcr + count;
	return gcr;
}
    
int GcrImage :: convert_disk_gcr2bin(BinImage *bin_image, UserInterface *user_interface)
{
    int errors = 0;
    int result = 0;
    for(int track=0;track<bin_image->num_tracks;track++) {
        result = convert_track_gcr2bin(track, bin_image);
        if(result)
            errors ++;
        if(user_interface)
            user_interface->update_progress(NULL, 1);
    }
    return errors;
}

int GcrImage :: convert_track_gcr2bin(int track, BinImage *bin_image)
{
	static uint8_t header[8];
	int t, s;

	int expected_secs = bin_image->track_sectors[track];
	uint8_t *bin = bin_image->track_start[track];
	uint8_t *gcr;
    uint8_t *begin;
	uint8_t *end;
	uint8_t *dest;
    uint8_t *gcr_data;
	uint8_t *new_gcr;
	bool  expect_data = false;
	bool  wrapped = false;
    begin = track_address[2*track];
	end = begin + track_length[2*track];
	gcr = begin;
    
	int secs = 0;
	
	while(secs < expected_secs) {
        
		new_gcr = find_sync(gcr, begin, end);
        if(new_gcr < gcr) {
//            printf(":W");
            wrapped = true;
        }            
        gcr = new_gcr;

        gcr_data = wrap(&gcr, begin, end, 5);
		conv_5bytes_gcr2bin(&gcr_data, &header[0]);
		if(header[0] == 8) {
            gcr_data = wrap(&gcr, begin, end, 5);
			conv_5bytes_gcr2bin(&gcr_data, &header[4]);
			t = (int)header[3];
			s = (int)header[2];
			dest = bin + (256 * s);
//            printf(":H[%d.%d]", t, s);
//            if(s==8) {
//                printf("\nSector 8. Current pointer: %p\n", gcr_data);
//                dump_hex(new_gcr-10, 104);
//            }
			//printf("Sector header found: %d %d\n", t, s);
			if(t != (track+1)) {
				printf("Error, sector doesn't belong to this track.\n");
				return -1;
			}
			if(s >= expected_secs) {
				printf("Sector out of bounds.\n");
				return -2;
			}
			expect_data = true;
		}
		if(header[0] == 7) {
			if(!expect_data) {
//                printf("$");
//				printf("Ignoring data block. No header.\n");
			} else {
//                printf(":D");
				expect_data = false;
				secs++;
				memcpy(dest, &header[1], 3);
				dest += 3;
                gcr_data = wrap(&gcr, begin, end, 320);
				for(int i=0;i<63;i++) {
					conv_5bytes_gcr2bin(&gcr_data, dest);
					dest+=4;
				}
				conv_5bytes_gcr2bin(&gcr_data, &header[4]);
				*(dest++) = header[4];
				//printf("bin = %p\n", bin);
			}
		}
	}
	printf("%d sectors found.\n", secs);
	if(secs != expected_secs)
		return -3;

	return 0;
}

void GcrImage :: convert_disk_bin2gcr(BinImage *bin_image, UserInterface *user_interface)
{
	id1 = bin_image->bin_data[91554];
    id2 = bin_image->bin_data[91555];

    uint8_t *gcr = gcr_image; // internal storage
    uint8_t *newgcr;

    invalidate();

    for(int i=0;i<bin_image->num_tracks;i++) {
//        printf("Track %d starts at: %7x\n", i+1, gcr);
        track_address[2*i] = gcr;
        newgcr = convert_track_bin2gcr(i, bin_image->track_start[i], gcr, bin_image->errors, bin_image->error_size);
        track_length[2*i] = int(newgcr - gcr);
		track_address[2*i + 1] = dummy_track;
        track_length[2*i + 1] = int(newgcr - gcr);
        gcr = newgcr;
        if(user_interface)
            user_interface->update_progress(NULL, 1);
    }
}

bool GcrImage :: load(File *f)
{
    // first just load the whole damn thing in memory, up to C1541_MAX_GCR_LEN in length
    uint32_t bytes_read;
    uint32_t *pul, offset;
    uint8_t *tr;
    uint16_t w;

    FRESULT res = f->read(gcr_data, C1541_MAX_GCR_LEN, &bytes_read);

    printf("Total bytes read: %d.\n", bytes_read);
    if(res != FR_OK) {
    	return false;
    }
    // check signature
    pul = (uint32_t *)gcr_data;

    if (strncmp("GCR-1541", (char *)gcr_data, 8) != 0) {
        printf("Wrong header.\n");
        return false;
    }

    // extract parameters
    // track offsets start at 0x000c
    for(int i=0;i<C1541_MAXTRACKS;i++) {
    	offset = le_to_cpu_32(pul[i+3]);
    	if(offset > C1541_MAX_GCR_LEN) {
    		printf("Error. Track pointer outside GCR memory range.\n");
    		return false;
    	}
    	tr = gcr_data + offset;
    	if(offset) {
    		w = tr[0] | (uint16_t(tr[1]) << 8);
	    	track_address[i] = tr + 2;
    		track_length[i] = (int)w;
            printf("Set track %d.%d to 0x%6x / 0x%4x.\n", (i>>1)+1, (i&1)?5:0, track_address[i], w);
        } else {
            printf("Skipping track %d.%d.\n", (i>>1)+1, (i&1)?5:0);
            track_address[i] = dummy_track;
            track_length[i] = (int)w;
    	}
    }
    return true;
}

int GcrImage :: find_track_start(int track)
{
    uint8_t *begin = track_address[track];
    uint8_t *end   = track_address[track] + track_length[track];
    uint8_t *gcr = begin;
    uint8_t *gcr_data;
    int offset;
    
    for(int j=0;j<42;j++) { // try at maximum twice for each sector (two syncs per sector!)
        gcr = find_sync(gcr, begin, end);
        if(!gcr)
            return 0;
        gcr_data = wrap(&gcr, begin, end, 5);
    	conv_5bytes_gcr2bin(&gcr_data, &header[0]);
    	if((header[0] == 8)&&(header[2] == 0)) {
            // sector 0 found!
            offset = (gcr - begin) - 10;
            if(offset < 0)
                offset += track_length[track];
            return offset;
        }
    }
    return 0;    
}
    
bool GcrImage :: save(File *f, bool align, UserInterface *user_interface)
{
    uint8_t *header = new uint8_t[16 + C1541_MAXTRACKS * 8];

    memcpy(header, "GCR-1541", 8);
    header[8] = 0;
    header[9] = C1541_MAXTRACKS;
    header[10] = (uint8_t)C1541_MAXTRACKLEN;
    header[11] = (uint8_t)(C1541_MAXTRACKLEN >> 8);

    uint32_t *pul = (uint32_t *)&header[12]; // because 12 is a multiple of 4, we can do this

    uint32_t track_start = 12 + C1541_MAXTRACKS * 8;
    
    for(int i=0;i<C1541_MAXTRACKS;i++) {
        if((track_address[i] == dummy_track)||(!track_address[i]))
            *(pul++) = 0;
        else {
            *(pul++) = cpu_to_le_32(track_start);
            track_start += C1541_MAXTRACKLEN + 2;
            // track_start += track_length[i] + 2;
        }
    }
    int speed;
    for(int i=0;i<C1541_MAXTRACKS;i++) {
        if(i<34) speed = 3;
        else if(i<48) speed = 2;
        else if(i<60) speed = 1;
        else speed = 0;

        if((track_address[i] == dummy_track)||(!track_address[i]))
            *(pul++) = 0;
        else 
            *(pul++) = cpu_to_le_32(speed);
    }

    uint32_t bytes_written;
    FRESULT res;
    f->write(header, 12 + C1541_MAXTRACKS * 8, &bytes_written);
    delete header;
    
    if(bytes_written != 12 + C1541_MAXTRACKS * 8)
        return false;
        
    uint8_t *filler_bytes = new uint8_t[C1541_MAXTRACKLEN];
    memset(filler_bytes, 0xFF, C1541_MAXTRACKLEN);
    
    uint8_t size[2];
    int skipped = 0;
    for(int i=0;i<C1541_MAXTRACKS;i++) {
        if((track_address[i] == dummy_track)||(!track_address[i])) {
            skipped++;
            continue;
        }
        int reported_length = track_length[i];
        size[0] = (uint8_t)reported_length;
        size[1] = (uint8_t)(reported_length >> 8);
        f->write(size, 2, &bytes_written);
        if(bytes_written != 2)
            break;
        // find alignment
        int start = 0;
        if(align)
            start = find_track_start(i);
        if(start > 0) {
            res = f->write(track_address[i]+start, track_length[i]-start, &bytes_written);
            if(bytes_written != (track_length[i]-start))
                break;
            res = f->write(track_address[i], start, &bytes_written);
        } else {
        	res = f->write(track_address[i], track_length[i], &bytes_written);
        }
        if(res != FR_OK)
            break;
        res = f->write(filler_bytes, C1541_MAXTRACKLEN - track_length[i], &bytes_written);
        if(user_interface)
            user_interface->update_progress(NULL, 1 + skipped);
        if(res != FR_OK)
            break;
        skipped = 0;
    }
    
    delete filler_bytes;
    
    if(res != FR_OK)
        return false;
    return true;
}
    
bool GcrImage :: write_track(int track, File *f, bool align)
{
	if(track_address[track] == dummy_track)
		return false;

	uint32_t offset = uint32_t(track_address[track]) - uint32_t(gcr_data);
	uint32_t bytes_written, bw2;

	//int fres = fseek(f, offset, SEEK_SET);
	FRESULT res = f->seek(offset);
	if(res != FR_OK)
		return false;

    int start = 0;
    if(align)
        start = find_track_start(track);
    if(start > 0) {
    	res = f->write(track_address[track]+start, track_length[track]-start, &bytes_written);
        if(res != FR_OK)
    		return false;
        res = f->write(track_address[track], start, &bw2);
        bytes_written += bw2;
    } else {
    	res = f->write(track_address[track], track_length[track], &bytes_written);
    }
	if(res != FR_OK)
		return false;
    f->sync();
	printf("%d bytes written at offset %6x.\n", bytes_written, offset);
	return true;
}

bool GcrImage :: test(void)
{
    // first create a temporary binary image
    // and fill it with test data
    BinImage *bin = new BinImage("Test");
    bin->format("diskname");
    
    uint8_t *bin_track0 = bin->track_start[0];

    bin_track0[260] = 0; // 0x104 = 0
    bin_track0[261] = 0; // 0x105 = 0
    
    int sectors = bin->track_sectors[0];
    int bytes = sectors * 256;
    
    uint8_t b = 1;
    uint8_t *dst = bin_track0;
    for(int i=0;i<bytes;i++) {
        *(dst++) = b++;
        if(b == 45)
            b = 1;
    }

    // convert it to gcr
    convert_disk_bin2gcr(bin, NULL);

    // now let's say we decode back to another location:
    bin->track_start[0] = bin->track_start[2];
    uint8_t *decoded = bin->track_start[2];

    // determine where to put the wrap byte
    uint8_t *gcr_next = track_address[0] + track_length[0];
    
    printf("GCR Image ready to decode...\n");
    
    // now start decoding 600 times
    int total = 0;
    for(int i=0;i<600;i++) {
        // clear
        for(int j=0;j<bytes;j++)
            decoded[j] = 0;

        // decode
        convert_track_gcr2bin(0, bin);

        // compare (better than checking the return value)
        int errors = 0;
        for(int j=0;j<bytes;j++) {
            if (decoded[j] != bin_track0[j])
                errors ++;
        }
        printf("Decode position %d: Errors = %d\n", i, errors);
        total += errors;

        // shift up one byte
        *(gcr_next++) = *track_address[0];
        track_address[0] ++;
    }
    printf("Test was %s\n", total?"NOT successful":"SUCCESSFUL!!");
    delete bin;
    return (total == 0);    
}
    
//--------------------------------------------------------------
// Binary Image Class
//--------------------------------------------------------------
BinImage :: BinImage(const char *name)
{
	bin_data = new uint8_t[C1541_MAX_D64_LEN];
	int sects;
	uint8_t *track = bin_data;
	for(int i=0;i<C1541_MAXTRACKS;i++) {
		sects = sectors_per_track[track_to_region(i)];
		track_start[i] = track;
		track_sectors[i] = sects;
		track += (256 * sects);
	}
	errors = NULL;
	error_size = 0;
	num_tracks = 35;
	
    // we'll create a ram-mapped block device and a default
    // partition to attach our file system to, so we can access the
    // bin image as if it were a file system as well
    // actually, we could have made a ram-mapped partition as well directly, TODO
    blk = new BlockDevice_Ram(bin_data, 256, 768);
    prt = new Partition(blk, 0, 768, 0);
    fs  = new FileSystemD64(prt);
}

BinImage :: ~BinImage()
{
    //this->detach();
    //root.children.remove(this);
    
	if(bin_data)
		delete bin_data;
    if(fs)
        delete fs;
    if(prt)
        delete prt;
    if(blk)
        delete blk;
}

/*
int BinImage :: get_absolute_sector(int track, int sector)
{
    track --;
    if (track < 0)
        return -1;
        
    int result = track * 17;
    
    result += 2*max(track, 17);
    result += max(track, 24);
    result += max(track, 30);

    if(result >= 785) // 41 tracks limit
        return 784;
        
    return result;
}
*/

uint8_t * BinImage :: get_sector_pointer(int track, int sector)
{
	if (track < 1) {
		return 0;
	}
	if (sector < 0) {
		return 0;
	}
	if (track >= C1541_MAXTRACKS) {
		return 0;
	}
	if (sector >= track_sectors[track-1]) {
		return 0;
	}
    return track_start[track-1] + (sector << 8);
}

int BinImage :: copy(uint8_t *data, uint32_t size)
{
	if (size > C1541_MAX_D64_LEN)
		size = C1541_MAX_D64_LEN;
	memcpy(bin_data, data, size);
	return init(size);
}

int BinImage :: load(File *file)
{
	num_tracks = 0;

	FRESULT  res;
	uint32_t transferred = 0;

	res = file->seek(0);
	if(res != FR_OK)
		return -1;
	res = file->read(bin_data, C1541_MAX_D64_LEN, &transferred);
	if(res != FR_OK)
		return -2;
	printf("Transferred: %d bytes\n", transferred);
	return init(transferred);
}

int BinImage :: init(uint32_t size)
{
	if(size < C1541_MAX_D64_35_NO_ERRORS) {
		return -3;
	}
	size -= (C1541_MAX_D64_35_NO_ERRORS);
	num_tracks = 35;
	errors = &bin_data[C1541_MAX_D64_35_NO_ERRORS];
	while(size >= 17*256) {
		num_tracks ++;
		size -= 17*256;
		errors += 17*256;
	}
	error_size = (int)size;
	if(!size)
		errors = NULL;

	printf("Tracks: %d. Errors: %s\n", num_tracks, errors?"Yes":"No");
	return 0;
}

int BinImage :: save(File *file, UserInterface *user_interface)
{
	uint32_t transferred = 0;
	FRESULT res = file->seek(0);
	if(res != FR_OK) {
		printf("SEEK ERROR: %d\n", res);
		return -1;
	}

	uint8_t *data = bin_data;
    if(user_interface) {
        for(int i=0;i<34;i++) {
        	res = file->write(data, 10 * 512, &transferred);
        	if(res != FR_OK) {
                printf("WRITE ERROR: %d. Transferred = %d\n", res, transferred);
        		return -2;
            }
            user_interface->update_progress(NULL, 1);
            data += (10 * 512);
        }
    	res = file->write(data, 3 * 256, &transferred);
    	if(res != FR_OK) {
            printf("WRITE ERROR: %d. Transferred = %d\n", res, transferred);
    		return -2;
        }
    } else {
    	res = file->write(data, 683 * 256, &transferred);
    	if(res != FR_OK) {
            printf("WRITE ERROR: %d. Transferred = %d\n", res, transferred);
    		return -2;
        }
    }            

	data = &bin_data[683*256];
	num_tracks -= 35;
	while(num_tracks--) {
		res = file->write(data, 17*256, &transferred);
        if(user_interface)
            user_interface->update_progress(NULL, 1);
    	if(res != FR_OK) {
            printf("WRITE ERROR: %d. Transferred = %d\n", res, transferred);
			return -3;
        }
		data += 17*256;
	}
	if(errors) {
		res = file->write(errors, error_size, &transferred);
    	if(res != FR_OK) {
            printf("WRITE ERROR: %d. Transferred = %d\n", res, transferred);
			return -4;
	    }
	}
	return 0;
}

int BinImage :: format(const char *name)
{
	uint8_t *track_18 = &bin_data[17*21*256];
	uint8_t *bam_name = track_18 + 144;

	memset(bin_data, 0, C1541_MAX_D64_LEN);
	memcpy(track_18, bam_header, 144);

    // part that comes after bam header
    for(int t=0;t<27;t++) {
        bam_name[t] = 0xA0;
    }
    bam_name[21] = '2';
    bam_name[22] = 'A';

    char c;
    int b;
    for(int t=0,b=0;t<27;t++) {
        c = name[b++];
        if(!c)
            break;
        c = toupper(c);
        if(c == ',') {
        	t = 17;
        	continue;
        }
		bam_name[t] = (uint8_t)c;
    }
    track_18[257] = 0xFF; // second byte of second sector 00 FF .. .. ..

    num_tracks = 35;
    return 0;
}

int BinImage :: write_track(int track, GcrImage *gcr_image, File *file)
{
	int res = gcr_image->convert_track_gcr2bin(track, this);
	if(res) {
        printf("Decode failed with error %d.\n", res);
		return res;
	}
	uint32_t offset = uint32_t(track_start[track] - bin_data);
	res = file->seek(offset);
	if(res != 0) {
        printf("While trying to write track %d, seek offset $%6x failed with error %d.\n", track+1, offset, res);
		return res;
	}
	uint32_t transferred;
	FRESULT fres = file->write(track_start[track], 256*track_sectors[track], &transferred);
	if(fres != FR_OK)
		return res;
    return file->sync();
}

void BinImage :: get_sensible_name(char *buffer)
{
    buffer[0] = 0;
    Directory *r;
    fs->dir_open(NULL, &r);
    char *n;
    FileInfo fi(32);    
    r->get_entry(fi); // title
    for(int i=0;i<18;i++)
        fi.lfname[i] &= 0x7f; // remove reverse
    fi.lfname[18] = 0;
    for(int i=17;i>=0;i--)
        if (fi.lfname[i] == ' ')
            fi.lfname[i] = 0;
        else
            break;
    printf("Title: '%s'\n", fi.lfname);
    if(strlen(fi.lfname) == 0) { // no name? try next
        r->get_entry(fi); // title
    }
    strcpy(buffer, fi.lfname);
    int len = strlen(buffer);
    for(int i=0;i<len;i++) {
        if((buffer[i]>='A')&&(buffer[i]<='Z'))
            buffer[i] |= 0x20;
    }
    fs->dir_close(r);    
}

BinImage static_bin_image("Static Binary Image"); // for general use

int ImageCreator :: S_createD64(SubsysCommand *cmd)
{
	int doG64 = cmd->mode;
	char buffer[64];

	buffer[0] = 0;
	int res;
	BinImage *bin;
	GcrImage *gcr;
	FileManager *fm = FileManager :: getFileManager();
	bool save_result;

	res = cmd->user_interface->string_box("Give name for new disk..", buffer, 22);
	if(res > 0) {
		fix_filename(buffer);
	    bin = &static_bin_image; //new BinImage;
		if(bin) {
    		bin->format(buffer);
			set_extension(buffer, (doG64)?(char *)".g64":(char *)".d64", 32);
            File *f = 0;
            FRESULT fres = fm -> fopen(cmd->path.c_str(), buffer, FA_WRITE | FA_CREATE_NEW, &f);
			if(f) {
                if(doG64) {
                    gcr = new GcrImage;
                    if(gcr) {
                        bin->num_tracks = 40;
                        cmd->user_interface->show_progress("Converting..", 120);
                        gcr->convert_disk_bin2gcr(bin, cmd->user_interface);
                        cmd->user_interface->update_progress("Saving...", 0);
                        save_result = gcr->save(f, false, cmd->user_interface); // create image, without alignment, we are aligned already
                        cmd->user_interface->hide_progress();
                    } else {
                        printf("No memory to create gcr image.\n");
                        return -3;
                    }
                } else {
                    cmd->user_interface->show_progress("Creating D64..", 35);
                    save_result = bin->save(f, cmd->user_interface);
                    cmd->user_interface->hide_progress();
                }
        		printf("Result of save: %d.\n", save_result);
                fm->fclose(f);
			} else {
				printf("Can't create file '%s'\n", buffer);
				cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
				return -2;
			}
			// delete bin;
		} else {
			printf("No memory to create bin.\n");
			return -1;
		}
	}
	return 0;
}

// instantiate so that we exist
ImageCreator image_creator;
