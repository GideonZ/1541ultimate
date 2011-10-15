/*
 * disk_image.cc
 *
 *  Created on: Apr 14, 2010
 *      Author: Gideon
 */
#include "disk_image.h"
#include "file_system.h"
#include <ctype.h>
#include <string.h>
extern "C" {
	#include "dump_hex.h"
}
#include "userinterface.h" // for showing status information only

__inline DWORD le_to_cpu_32(DWORD a)
{
    DWORD m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
}

#define cpu_to_le_32 le_to_cpu_32


extern BYTE bam_header[];

#define HARDWARE_ENCODING 1

// Single nibble GCR table
const BYTE gcr_table[] = { 0x0A, 0x0B, 0x12, 0x13, 0x0E, 0x0F, 0x16, 0x17,
                           0x09, 0x19, 0x1A, 0x1B, 0x0D, 0x1D, 0x1E, 0x15 };

const int track_lengths[] =      { 0x1E00, 0x1BE0, 0x1A00, 0x1860, 0x1E00, 0x1BE0, 0x1A00, 0x1860 };
const int sectors_per_track [] = { 21, 19, 18, 17, 21, 19, 18, 17 };
const int region_end[] =         { 17, 24, 30, 35, 52, 59, 65, 70 };
const int sector_gap_lengths[] = {  9, 19, 13, 10,  9, 19, 13, 10 };

#if HARDWARE_ENCODING == 0
static BYTE gcr_table_hi_0[256];
static BYTE gcr_table_lo_0[256];
static BYTE gcr_table_hi_2[256];
static BYTE gcr_table_lo_2[256];
static BYTE gcr_table_hi_4[256];
static BYTE gcr_table_lo_4[256];
static BYTE gcr_table_hi_6[256];
static BYTE gcr_table_lo_6[256];
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


GcrImage :: GcrImage(void)
{
#if HARDWARE_ENCODING == 0
    WORD gcr;
    if(!(gcr_table_initialized)) {
        for(int i=0;i<256;i++) {
            gcr = gcr_table[i & 15] | (WORD(gcr_table[i >> 4]) << 5);
            gcr_table_hi_0[i] = BYTE(gcr >> 8);
            gcr_table_lo_0[i] = BYTE(gcr & 0xFF);
            gcr <<= 2;
            gcr_table_hi_2[i] = BYTE(gcr >> 8);
            gcr_table_lo_2[i] = BYTE(gcr & 0xFF);
            gcr <<= 2;
            gcr_table_hi_4[i] = BYTE(gcr >> 8);
            gcr_table_lo_4[i] = BYTE(gcr & 0xFF);
            gcr <<= 2;
            gcr_table_hi_6[i] = BYTE(gcr >> 8);
            gcr_table_lo_6[i] = BYTE(gcr & 0xFF);
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

    BYTE *gcr = gcr_image; // internal storage
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

BYTE *GcrImage :: convert_block_bin2gcr(BYTE *bin, BYTE *gcr, int len)
{
	for(int i=0;i<len;i+=4) {
#if HARDWARE_ENCODING > 0
    	GCR_ENCODER_BIN_IN = *(bin++);
    	GCR_ENCODER_BIN_IN = *(bin++);
    	GCR_ENCODER_BIN_IN = *(bin++);
    	GCR_ENCODER_BIN_IN = *(bin++);
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

BYTE *GcrImage :: convert_track_bin2gcr(int track, BYTE *bin, BYTE *gcr)
{
    sector_buffer[0] = 7;
    sector_buffer[258] = 0;
    sector_buffer[259] = 0;

    header[0] = 8;
    header[3] = (BYTE)track+1;
    header[4] = id2;
    header[5] = id1;
    header[6] = 0x0f;
    header[7] = 0x0f;

    int region = track_to_region(track);

    BYTE *bp, chk, b;
    BYTE *end = gcr + track_lengths[region];

    for(int s=0;s<sectors_per_track[region];s++) {
        header[2] = (BYTE)s;
        // calculate header checksum
        header[1] = header[2] ^ header[3] ^ header[4] ^ header[5];

        // put 5 sync bytes
        for(int i=0;i<5;i++)
            *(gcr++) = 0xFF;

        gcr = convert_block_bin2gcr(header, gcr, 8);

        // put 9 gap bytes
        for(int i=0;i<9;i++)
            *(gcr++) = 0x55;

        // put 5 sync bytes
        for(int i=0;i<5;i++)
            *(gcr++) = 0xFF;

        // encapsulate sector with '7' byte as header and checksum at the end
        bp = &sector_buffer[1];
        chk = 0;
        for(int i=0;i<256;i++) {
            b = *(bin++);
            chk ^= b;
            *(bp++) = b;
        }
        *(bp) = chk;

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

BYTE *GcrImage :: find_sync(BYTE *gcr_data, BYTE *begin, BYTE *end)
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

inline void conv_5bytes_gcr2bin(BYTE **gcr, BYTE *bin)
{
	BYTE *b = *gcr;
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

BYTE *GcrImage :: wrap(BYTE **current, BYTE *begin, BYTE *end, int count)
{
    BYTE *gcr = *current;
//    printf("[%p-%p-%p:%d]", begin, gcr, end, count);
    if(gcr > (end - count)) {
//		printf("Wrap(%d)..", count);
		BYTE *d = sector_buffer;
		BYTE *s = gcr;
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
    
int GcrImage :: convert_disk_gcr2bin(BinImage *bin_image, bool report)
{
    int errors = 0;
    int result = 0;
    for(int track=0;track<bin_image->num_tracks;track++) {
        result = convert_track_gcr2bin(track, bin_image);
        if(result)
            errors ++;
        if(report)
            user_interface->update_status(NULL, 1);
    }
    return errors;
}

int GcrImage :: convert_track_gcr2bin(int track, BinImage *bin_image)
{
	static BYTE header[8];
	int t, s;

	int expected_secs = bin_image->track_sectors[track];
	BYTE *bin = bin_image->track_start[track];
	BYTE *gcr;
    BYTE *begin;
	BYTE *end;
	BYTE *dest;
    BYTE *gcr_data;
	BYTE *new_gcr;
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

void GcrImage :: convert_disk_bin2gcr(BinImage *bin_image, bool report)
{
	id1 = bin_image->bin_data[91554];
    id2 = bin_image->bin_data[91555];

    BYTE *gcr = gcr_image; // internal storage
    BYTE *newgcr;

    invalidate();

    for(int i=0;i<bin_image->num_tracks;i++) {
//        printf("Track %d starts at: %7x\n", i+1, gcr);
        track_address[2*i] = gcr;
        newgcr = convert_track_bin2gcr(i, bin_image->track_start[i], gcr);
        track_length[2*i] = int(newgcr - gcr);
		track_address[2*i + 1] = dummy_track;
        track_length[2*i + 1] = int(newgcr - gcr);
        gcr = newgcr;
        if(report)
            user_interface->update_status(NULL, 1);
    }
}

bool GcrImage :: load(File *f)
{
    // first just load the whole damn thing in memory, up to C1541_MAX_GCR_LEN in length
    UINT  bytes_read;
    DWORD *pul, offset;
    BYTE *tr;
    FRESULT fres;
    WORD w;

    fres = f->read(gcr_data, C1541_MAX_GCR_LEN, &bytes_read);

    printf("Total bytes read: %d.\n", bytes_read);
    if(fres != FR_OK) {
    	return false;
    }
    // check signature
    pul = (DWORD *)gcr_data;
/*
	printf("Header: %8x %8x\n", pul[0], pul[1]);
	dump_hex(gcr_data, 64);
*/	
    if((pul[0] != 0x4743522D)||(pul[1] != 0x31353431)) { // big endianness assumed
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
    		w = tr[0] | (WORD(tr[1]) << 8);
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
    BYTE *begin = track_address[track];
    BYTE *end   = track_address[track] + track_length[track];
    BYTE *gcr = begin;
    BYTE *gcr_data;
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
    
bool GcrImage :: save(File *f, bool report, bool align)
{
    BYTE *header = new BYTE[16 + C1541_MAXTRACKS * 8];

    memcpy(header, "GCR-1541", 8);
    header[8] = 0;
    header[9] = C1541_MAXTRACKS;
    header[10] = (BYTE)C1541_MAXTRACKLEN;
    header[11] = (BYTE)(C1541_MAXTRACKLEN >> 8);

    DWORD *pul = (DWORD *)&header[12]; // because 12 is a multiple of 4, we can do this

    DWORD track_start = 12 + C1541_MAXTRACKS * 8;
    
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

    UINT bytes_written;
    FRESULT res = f->write(header, 12 + C1541_MAXTRACKS * 8, &bytes_written);
    delete header;
    
    if(res != FR_OK)
        return false;
        
    BYTE *filler_bytes = new BYTE[C1541_MAXTRACKLEN];
    memset(filler_bytes, 0xFF, C1541_MAXTRACKLEN);
    
    BYTE size[2];
    int skipped = 0;
    for(int i=0;i<C1541_MAXTRACKS;i++) {
        if((track_address[i] == dummy_track)||(!track_address[i])) {
            skipped++;
            continue;
        }
        int reported_length = track_length[i];
        size[0] = (BYTE)reported_length;
        size[1] = (BYTE)(reported_length >> 8);
        res = f->write(size, 2, &bytes_written);
        if(res != FR_OK)
            break;
        // find alignment
        int start = 0;
        if(align)
            start = find_track_start(i);
        if(start > 0) {
            res = f->write(track_address[i]+start, track_length[i]-start, &bytes_written);
            if(res != FR_OK)
                break;
            res = f->write(track_address[i], start, &bytes_written);
        } else {
            res = f->write(track_address[i], track_length[i], &bytes_written);
        }
        if(res != FR_OK)
            break;
        res = f->write(filler_bytes, C1541_MAXTRACKLEN - track_length[i], &bytes_written);
        if(report)
            user_interface->update_status(NULL, 1 + skipped);
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

	DWORD offset = DWORD(track_address[track]) - DWORD(gcr_data);
	UINT bytes_written, bw2;

	FRESULT fres = f->seek(offset);
	if(fres != FR_OK)
		return false;

    int start = 0;
    if(align)
        start = find_track_start(track);
    if(start > 0) {
        fres = f->write(track_address[track]+start, track_length[track]-start, &bytes_written);
        if(fres != FR_OK)
    		return false;
        fres = f->write(track_address[track], start, &bw2);
        bytes_written += bw2;
    } else {
        fres = f->write(track_address[track], track_length[track], &bytes_written);
    }
	if(fres != FR_OK)
		return false;
    f->sync();
	printf("%d bytes written at offset %6x.\n", bytes_written, offset);
	return true;
}

bool GcrImage :: test(void)
{
    // first create a temporary binary image
    // and fill it with test data
    BinImage *bin = new BinImage;
    bin->format("diskname");
    
    BYTE *bin_track0 = bin->track_start[0];

    bin_track0[260] = 0; // 0x104 = 0
    bin_track0[261] = 0; // 0x105 = 0
    
    int sectors = bin->track_sectors[0];
    int bytes = sectors * 256;
    
    BYTE b = 1;
    BYTE *dst = bin_track0;
    for(int i=0;i<bytes;i++) {
        *(dst++) = b++;
        if(b == 45)
            b = 1;
    }

    // convert it to gcr
    convert_disk_bin2gcr(bin, false);

    // now let's say we decode back to another location:
    bin->track_start[0] = bin->track_start[2];
    BYTE *decoded = bin->track_start[2];

    // determine where to put the wrap byte
    BYTE *gcr_next = track_address[0] + track_length[0];
    
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
    return (total == 0);    
}
    
//--------------------------------------------------------------
// Binary Image Class
//--------------------------------------------------------------
BinImage :: BinImage()
{
	bin_data = new BYTE[C1541_MAX_D64_LEN];

	int sects;
	BYTE *track = bin_data;
	for(int i=0;i<C1541_MAXTRACKS;i++) {
		sects = sectors_per_track[track_to_region(i)];
		track_start[i] = track;
		track_sectors[i] = sects;
		track += (256 * sects);
	}
	errors = NULL;
	num_tracks = 35;
}

BinImage :: ~BinImage()
{
	if(bin_data)
		delete bin_data;
}

int BinImage :: load(File *file)
{
	num_tracks = 0;

	int res;
	UINT transferred = 0;
	res =  file->seek(0);
	if(res != FR_OK)
		return -1;
	res = file->read(bin_data, C1541_MAX_D64_LEN, &transferred);
	if(res != FR_OK)
		return -2;

	printf("Transferred: %d bytes\n", transferred);
	if(transferred < (683*256)) {
		return -3;
	}
	transferred -= (683*256);
	num_tracks = 35;
	errors = &bin_data[683*256];
	while(transferred >= 17*256) {
		num_tracks ++;
		transferred -= 17*256;
		errors += 17*256;
	}
	error_size = (int)transferred;
	if(!transferred)
		errors = NULL;

	printf("Tracks: %d. Errors: %s\n", num_tracks, errors?"Yes":"No");
	return 0;
}

int BinImage :: save(File *file, bool report)
{
	UINT transferred = 0;
	int res =  file->seek(0);
	if(res != FR_OK) {
		printf("SEEK ERROR: %d\n", res);
		return -1;
	}

	BYTE *data = bin_data;
    if(report) {
        for(int i=0;i<34;i++) {
        	res = file->write(data, 10 * 512, &transferred);
        	if(res != FR_OK)
        		return -2;
            user_interface->update_status(NULL, 1);
            data += (10 * 512);
        }
    	res = file->write(data, 3 * 256, &transferred);
    	if(res != FR_OK)
    		return -2;
    } else {
    	res = file->write(data, 683 * 256, &transferred);
    	if(res != FR_OK)
    		return -2;
    }            

	data = &bin_data[683*256];
	num_tracks -= 35;
	while(num_tracks--) {
		res = file->write(data, 17*256, &transferred);
        if(report)
            user_interface->update_status(NULL, 1);
		if(res != FR_OK)
			return -3;
		data += 17*256;
	}
	if(errors) {
		res = file->write(errors, error_size, &transferred);
		if(res != FR_OK)
			return -4;
	}
	return 0;
}

int BinImage :: format(char *name)
{
	BYTE *track_18 = &bin_data[17*21*256];
	BYTE *bam_name = track_18 + 144;

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
		bam_name[t] = (BYTE)c;
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
	DWORD offset = DWORD(track_start[track] - bin_data);
	res = file->seek(offset);
	if(res != FR_OK) {
        printf("While trying to write track %d, seek offset $%6x failed with error %d.\n", track+1, offset, res);
		return res;
	}
	UINT transferred;
	res = file->write(track_start[track], 256*track_sectors[track], &transferred);
//    printf("Writing track %d. Trying %d bytes from offset $%06x, written: %d. Result=%d.\n", track+1, 256*track_sectors[track], offset, transferred, res);
	if(res != FR_OK)
		return res;
    return file->sync();
}

