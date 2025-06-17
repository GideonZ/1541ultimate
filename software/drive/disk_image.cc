/*
 * disk_image.cc
 *
 *  Created on: Apr 14, 2010
 *      Author: Gideon
 */
#include "disk_image.h"
#include "file_system.h"
#include "filemanager.h"
#include "return_codes.h"

#include <ctype.h>
#include <string.h>
#include <unistd.h>

extern "C" {
	#include "dump_hex.h"
}
#include "userinterface.h" // for showing status information only
#include "user_file_interaction.h"
#include "blockdev_file.h"
#include "endianness.h"

#define HARDWARE_ENCODING 1

// Single nibble GCR table
const uint8_t gcr_table[] = { 0x0A, 0x0B, 0x12, 0x13, 0x0E, 0x0F, 0x16, 0x17,
                           0x09, 0x19, 0x1A, 0x1B, 0x0D, 0x1D, 0x1E, 0x15 };

const int track_lengths[] =      { 0X1E0C, 0x1BE6, 0x1A0A, 0x186A, 0X1E0C, 0x1BE6, 0x1A0A, 0x186A };
const int sectors_per_track [] = { 21, 19, 18, 17, 21, 19, 18, 17 };
const int region_end[] =         { 17, 24, 30, 35, 52, 59, 65, 70 };
const int sector_gap_lengths[] = {  9, 19, 13, 10,  9, 19, 13, 10 };
const int region_speed_codes[] = {  3,  2,  1,  0,  3,  2,  1,  0 };

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

// Track number is ZERO BASED, whole tracks
static int track_to_region(int track, bool ds)
{
    int regions = ds ? 8 : 4;
    for(int i=0;i<regions;i++) {
        if(track < region_end[i])
            return i;
    }
    return regions - 1; // illegal
}

// Track number is ZERO BASED, whole tracks
static int total_sectors_before_track(int track, bool ds)
{
int count = 0;	
	for(int i=0;i<track;i++) {
		int region = track_to_region(i, ds);
		count += sectors_per_track[region];		
	}
	return count;
}

static int bin_track_to_gcr_track(int lt, bool ds)
{
    if (lt < 0) {
        return 0;
    }
    int ret = 2*lt;
    if (ds && (lt >= 35)) {
        ret = GCRIMAGE_FIRSTTRACKSIDE1 + 2*(lt - 35);
    }
    if (ret >= GCRIMAGE_MAXHDRTRACKS) {
        ret = GCRIMAGE_MAXHDRTRACKS - 1;
    }
    return ret;
}

static int gcr_track_to_bin_track(int pt)
{
    int ret = pt / 2; // by default remove half tracks
    if (pt >= GCRIMAGE_FIRSTTRACKSIDE1) { // side 1, starts at track logical track 35 (0 - 34 is on side 0)
        ret = 35 + (pt - GCRIMAGE_FIRSTTRACKSIDE1) / 2;
    }
    return ret;
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
    gcr_data  = new uint8_t[GCRIMAGE_MAXSIZE];
    double_sided = false;

	memset(gcr_data, 0x00, GCRIMAGE_MAXSIZE);
    invalidate();
}

GcrImage :: ~GcrImage(void)
{
    // free
    delete[] gcr_data;
}


void GcrImage :: dump(void)
{
    printf("Trk#  Addr   Len Sp UIM | Addr   Len Sp UIM  Disk is %s sided\n", double_sided ? "double" : "single");

    for(int i=0;i < GCRIMAGE_FIRSTTRACKSIDE1;i++) {
        int j = i + GCRIMAGE_FIRSTTRACKSIDE1;
        printf("%2d.%d: ", 1+(i/2), i&1 ? 5 : 0);
        if (tracks[i].track_address) {
            printf("%6x %4x %d %c%c%c | ", tracks[i].track_address, tracks[i].track_length, tracks[i].speed_zone,
                    tracks[i].track_used ? 'U' : ' ', tracks[i].in_image_file ? 'I' : ' ', tracks[i].track_is_mfm ? 'M' : ' ');
        } else {
            printf("------ ---- - --- | ");
        }
        if (tracks[j].track_address) {
            printf("%6x %4x %d %c%c%c\n", tracks[j].track_address, tracks[j].track_length, tracks[j].speed_zone,
                    tracks[j].track_used ? 'U' : ' ', tracks[j].in_image_file ? 'I' : ' ', tracks[j].track_is_mfm ? 'M' : ' ');
        } else {
            printf("------ ---- - ---\n");
        }
    }
}

void GcrImage :: invalidate(void)
{
    memset(tracks, 0, sizeof(tracks));

    // initially the ds flag is set to false, but when a track gets written on side 1
    // the flag is set to true.
    set_ds(false);
}

void GcrImage :: blank(void)
{
	invalidate();
	add_blank_tracks(gcr_data);
}

void GcrImage :: add_blank_tracks(uint8_t *gcr)
{
    int length, speed_zone;
    uint8_t *blank_start = gcr;
    uint8_t *address_limit = gcr_data + GCRIMAGE_MAXSIZE;

    // Install 40 valid tracks on each side, using only the whole tracks
    for(int i=0; i < GCRIMAGE_MAXUSEDTRACKS; i+=2) {
        // i is zero based track number times 2.
        // This equals the offset on side 0 for whole tracks
        length = track_lengths[track_to_region(i/2, false)];
        speed_zone = region_speed_codes[track_to_region(i/2, false)];

        // skip if track is aleady defined
        if (!tracks[i].track_address) {
            // stop if added track doesn't fit
            if (gcr + length <= address_limit) {
                tracks[i].track_address = gcr;
                tracks[i].track_length = length;
                tracks[i].speed_zone = speed_zone;
                gcr += length;
            }
        }

        if (!double_sided) {
            continue;
        }

        // Now for side 1, with the same track length
        // skip if track is aleady defined
        if (!tracks[i + GCRIMAGE_FIRSTTRACKSIDE1].track_address) {
            // stop if added track doesn't fit
            if (gcr + length <= address_limit) {
                tracks[i + GCRIMAGE_FIRSTTRACKSIDE1].track_address = gcr;
                tracks[i + GCRIMAGE_FIRSTTRACKSIDE1].track_length = length;
                tracks[i + GCRIMAGE_FIRSTTRACKSIDE1].speed_zone = speed_zone;
                gcr += length;
            }
        }
    }
    uint32_t fill_size = gcr - blank_start;
    memset(blank_start, 0, fill_size);
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

uint8_t *GcrImage :: convert_track_bin2gcr(uint8_t logical_track_1b, int region, uint8_t *bin, uint8_t *gcr, uint8_t *errors, int errors_size, int geosgaps)
{
	uint8_t errorcode;

    uint8_t *bp, chk, b;
    uint8_t *end = gcr + track_lengths[region];

    for(uint8_t s=0;s<sectors_per_track[region];s++) {
		sector_buffer[0] = 7;
		sector_buffer[258] = 0;
		sector_buffer[259] = 0;

		header[0] = 8;
		header[3] = logical_track_1b;
		header[4] = id2;
		header[5] = id1;
		header[6] = 0x0f;
		header[7] = 0x0f;
		errorcode = 0;
		if (errors != NULL && s < errors_size){
			errorcode = errors[s];
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
				header[4] = (id2 ^ ~(s+0x55));
				header[5] = (id1 ^ ~(s+0xAA));
				break;
		}		
		if (errorcode != 3) {
			// put 5 sync bytes
			for(int i=0;i<5;i++)
				*(gcr++) = 0xFF;
		} else {
			for(int i=0;i<5;i++)
				*(gcr++) = 0x00;
		}
        gcr = convert_block_bin2gcr(header, gcr, 8);

        // put 9 gap bytes
        for(int i=0;i<9;i++)
            *(gcr++) = 0x55;

       if (geosgaps & 1) {
        *(gcr-1) = 0x67;
        *(gcr-4) = 0x67;
       }

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

       if (geosgaps & 1) {
         *(gcr-1) = 0x67;
         *(gcr-4) = 0x67;
       }
    }

    // fill up to the end with gap
    while(gcr < end)
        *(gcr++) = 0x55;

    if (geosgaps & 1)
        *(gcr-1) = 0x67;

    return gcr;
}

uint8_t *GcrImage :: find_sync(uint8_t *gcr_data, uint8_t *begin, uint8_t *end)
{
    bool wrap = false;
    int sync_count = 0;

    do {
        if(gcr_data >= end) {
            gcr_data = begin;
            if (wrap) {
                return NULL;
            }
            wrap = true;
        }
        if(*gcr_data == 0xFF) {
            sync_count++;
        } else {
            if(sync_count >= 2)
                return gcr_data; // byte after sync
            sync_count = 0;
        }
        gcr_data++;
    } while(1);
    return NULL;
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

uint8_t *GcrImage ::wrap(uint8_t **current, uint8_t *begin, uint8_t *end, int count, uint8_t *buffer, uint8_t shift)
{
    uint8_t *gcr = *current;

    if (shift == 0) {
        if (gcr > (end - count)) {
            uint8_t *d = buffer;
            uint8_t *s = gcr;
            *current = (gcr + count) - (end - begin);
            while (count--) {
                if (s == end)
                    s = begin;
                *(d++) = *(s++);
            }
            return buffer; // set decode pointer to the scratch pad
        }
        *current = gcr + count;
        return gcr;
    } else {
        uint8_t *d = buffer;
        uint8_t *s = gcr;
        uint8_t curByte = *(s++) << shift;
        if (gcr > (end - count))
            *current = (gcr + count) - (end - begin);
        else
            *current = gcr + count;
        while (count--) {
            if (s == end)
                s = begin;
            curByte |= *s >> (8 - shift);
            *(d++) = curByte;
            curByte = *s << shift;
            s++;
        }
        return buffer; // set decode pointer to the scratch pad
    }
}

int GcrImage :: convert_disk_gcr2bin(BinImage *bin_image, UserInterface *user_interface)
{
    int errors = 0;
    int result = 0;

    // We don't know yet what the output size is going to be. However, we do know if the output disk is single or double sided.
    // It is necessary to 'format' the disk correctly to accommodate the tracks.
    if (double_sided) {
        bin_image->init(C1541_D71_SIZE_WITH_ERRORS);
    } else {
        bin_image->init(C1541_MAX_D64_40_WITH_ERRORS);
    }

    // Try pushing all valid tracks into the bin image
    int valid_tracks = 0;
    for(int pt=0; pt < GCRIMAGE_MAXHDRTRACKS; pt++) {
        GcrTrack *tr = &(tracks[pt]);
        if (!tr->track_address) {
            continue;
        }
        int secs = 0;
        result = bin_image->write_track(pt, this, NULL, errors, secs);
        if (secs > 0) {
            valid_tracks++;
        }
        if(user_interface)
            user_interface->update_progress(NULL, 1);
    }
    printf("Setting number of valid tracks in bin image to %d.\n", valid_tracks);
    bin_image->num_tracks = valid_tracks;
    return errors;
}

int GcrImage::convert_gcr_track_to_bin(uint8_t *gcr, int trackNumber, int trackLen, int maxSector, uint8_t *bin, uint8_t *status, int statlen)
{
    static uint8_t header[8];
    int t, s;

    uint8_t *begin;
    uint8_t *end;
    uint8_t *current;
    uint8_t *pntr;
    uint8_t *dest;
    uint8_t *gcr_data;
    uint8_t *new_gcr;
    uint8_t *st = status;

    bool expect_data = false;
    bool wrapped = false;

    current = gcr;
    begin = gcr;
    end = begin + trackLen;
    gcr = begin;

    int secs = 0;
    uint8_t sector_buffer[352];

    while (secs < maxSector) {

        new_gcr = find_sync(current, begin, end);
        if (!new_gcr) {
            break; // no sync found
        }
        if (new_gcr < current) {
            if (wrapped) {
                break;
            }
            wrapped = true;
        }
        pntr = current = new_gcr;

        uint8_t shift = 0;
        uint8_t firstByte = *new_gcr;

        while (firstByte & 0x80) {
            shift++;
            firstByte <<= 1;
            if (shift == 8)
                break;
        }

        gcr_data = wrap(&pntr, begin, end, 5, sector_buffer, shift);
        conv_5bytes_gcr2bin(&gcr_data, &header[0]);
        if (header[0] == 8) {
            gcr_data = wrap(&pntr, begin, end, 5, sector_buffer, shift);
            conv_5bytes_gcr2bin(&gcr_data, &header[4]);
            t = (int)header[3];
            s = (int)header[2];
            dest = bin + (256 * s);

            // We found a header, but we are expecting data
            if (expect_data) {
                if (st && (statlen > 0)) {
                    *(st++) = 0xE4;
                    statlen--;
                }
            }

            // new sector, store sector number
            if (st && (statlen > 0)) {
                *(st++) = s;
                statlen--;
            }
            expect_data = true;

            if (t != trackNumber) {
                if (st && (statlen > 0)) {
                    *(st++) = 0xE1;
                    statlen--;
                    expect_data = false;
                }
            } else if (s >= maxSector) {
                if (st && (statlen > 0)) {
                    *(st++) = 0xE2;
                    statlen--;
                    expect_data = false;
                }
            }
            if (expect_data) {
                current = pntr;
            }
            continue;
        }

        if (header[0] == 7) {
            if (!expect_data) {

            } else {
                expect_data = false;
                secs++;
                uint8_t *binarySector = dest;
                memcpy(dest, &header[1], 3);
                dest += 3;
                gcr_data = wrap(&pntr, begin, end, 320, sector_buffer, shift);
                for (int i = 0; i < 63; i++) {
                    conv_5bytes_gcr2bin(&gcr_data, dest);
                    dest += 4;
                }
                conv_5bytes_gcr2bin(&gcr_data, &header[4]);
                *(dest++) = header[4];

                if (st && (statlen > 0)) {
                    uint8_t chk = 0;
                    for (int i = 0; i < 256; i++) {
                        chk ^= *(binarySector++);
                    }
                    if (chk != header[5]) {
                        *(st++) = 0xE3;
                    } else {
                        *(st++) = 0x00;
                        current = pntr;
                    }
                    statlen--;
                }
            }
        }
    }
    return secs;
}

void GcrImage :: convert_disk_bin2gcr(BinImage *bin_image, UserInterface *user_interface, int geoscopyprot)
{
	id1 = bin_image->bin_data[91554];
    id2 = bin_image->bin_data[91555];

    uint8_t *gcr = gcr_data; // internal storage
    uint8_t *newgcr;

    invalidate();
    double_sided = bin_image->double_sided;

    // Clear all errors beforehand, in case they don't all get overwritten
    if (bin_image->errors) {
        memset(bin_image->errors, 0, bin_image->error_size);
    }

    // Loop over all logical tracks
    for(int lt=0; lt < bin_image->num_tracks; lt++) {
        // Calculate the physical track index
        int pt = bin_track_to_gcr_track(lt, double_sided);
        // Calculate first sector number of track
        int sec = total_sectors_before_track(lt, double_sided);
        // Select region for additional parameters
        int region = track_to_region(lt, double_sided);
        // Select error bytes
        uint8_t *error_bytes = NULL;
        int error_size = 0;
        if (bin_image->errors) {
            error_bytes = bin_image->errors + sec;
            error_size  = bin_image->error_size - sec;
        }
        tracks[pt].track_address = gcr;
        newgcr = convert_track_bin2gcr((uint8_t)(lt + 1), region, bin_image->track_start[lt], gcr, error_bytes, error_size,  geoscopyprot & 1);
        tracks[pt].track_length = int(newgcr - gcr);
        tracks[pt].speed_zone = region_speed_codes[region];
        tracks[pt].track_used = true;
        // printf("Convert_disk: Track %d => %d. Addr = %6x\n. Len = %4x", lt, pt, gcr, tracks[pt].track_length);
        gcr = newgcr;
        if(user_interface)
            user_interface->update_progress(NULL, 1);
    }

    //printf("DEBUG: geoscopyprot=%i, bin_image->num_tracks=%i\n", geoscopyprot, bin_image->num_tracks);
    if ((geoscopyprot & 2) && (bin_image->num_tracks == 35)) {
        // printf("DEBUG: Enter track 36 generation\n");
        int pt = 70;
        tracks[pt].track_length = 7692;
        tracks[pt].track_address = gcr;
        tracks[pt].speed_zone = 3;
        tracks[pt].track_used = true;
        for (int i = 0; i < 7692; i++)
            gcr[i] = 0x55;

        uint8_t data[12] = {0x2f, 0x53, 0x77, 0x7d, 0x67, 0x45, 0xb5, 0xdd, 0x77, 0x62, 0x73, 0x77};

        for (int s = 0; s < 0x18; s++) {
            for (int l = 0; l < 4; l++) {
                for (int i = 0; i < 0x15; i++) {
                    *(newgcr++) = data[3 * l + 0];
                    *(newgcr++) = data[3 * l + 1];
                    *(newgcr++) = data[3 * l + 2];
                }
            }

            for (int l = 0; l < 4; l++) {
                uint8_t ovl0 = data[3 * l] > 127 ? 1 : 0;
                uint8_t ovl1 = data[3 * l + 1] > 127 ? 1 : 0;
                uint8_t ovl2 = data[3 * l + 2] > 127 ? 1 : 0;

                data[3 * l + 2] = (data[3 * l + 2] << 1) + ovl0;
                data[3 * l + 1] = (data[3 * l + 1] << 1) + ovl2;
                data[3 * l + 0] = (data[3 * l + 0] << 1) + ovl1;
            }
        }
        gcr += 7692;
    }

    add_blank_tracks(gcr);
    dump();
}

bool GcrImage :: load(File *f)
{
    // first just load the whole damn thing in memory, up to C1541_MAX_GCR_LEN in length
    // This space should be enough for 84 tracks, which is single sided plus all half tracks, or double sided without half tracks.
    uint32_t bytes_read;
    uint32_t *pul, offset;
    uint8_t *tr;
    uint16_t w = 0x1E0C;

    invalidate();
    FRESULT res = f->read(gcr_data, GCRIMAGE_MAXSIZE, &bytes_read);

    printf("Total bytes read: %d.\n", bytes_read);
    if(res != FR_OK) {
    	return false;
    }
    // check signature
    pul = (uint32_t *)gcr_data;
    int max_tracks;

    if (strncmp("GCR-1541", (char *)gcr_data, 8) == 0) {
        max_tracks = 84;
    } else if(strncmp("GCR-1571", (char *)gcr_data, 8) == 0) { // double sided mode
        max_tracks = 168;
    } else {
        printf("Wrong header.\n");
        return false;
    }

    double_sided = false;
    // extract parameters
    // track offsets start at 0x000c
    for(int i=0;i<max_tracks;i++) {
    	offset = le_to_cpu_32(pul[i+3]);
    	if(offset > GCRIMAGE_MAXSIZE) {
    		printf("Error. Track pointer outside GCR memory range.\n");
    		return false;
    	}
    	tr = gcr_data + offset;
    	if(offset) {
    		w = tr[0] | (uint16_t(tr[1]) << 8);
	    	tracks[i].track_address = tr + 2;
    		tracks[i].track_length = (int)(w & 0x3FFF);
    		tracks[i].track_used = true;
    		tracks[i].in_image_file = true;
    		tracks[i].track_is_mfm = (w & 0x8000);
//            printf("Set track %d.%d to 0x%6x / 0x%4x.\n", (i>>1)+1, (i&1)?5:0, tracks[i].track_address, w);
            if (i >= GCRIMAGE_FIRSTTRACKSIDE1) {
                double_sided = true;
            }
        }
    }
    // Track speed zone info starts after the track offsets
    uint8_t reported_tracks = gcr_data[9];
    uint32_t *szone = pul + (3 + reported_tracks);

    for(int i=0;i<max_tracks;i++) {
        tracks[i].speed_zone = le_to_cpu_32(*(szone++));
    }

    add_blank_tracks(gcr_data + bytes_read);
    dump();
    return true;
}

int GcrImage :: find_track_start(int track)
{
    uint8_t *begin = tracks[track].track_address;
    uint8_t *end   = begin + tracks[track].track_length;
    uint8_t *gcr = begin;
    uint8_t *gcr_data;
    int offset;
    
    for(int j=0;j<42;j++) { // try at maximum twice for each sector (two syncs per sector!)
        gcr = find_sync(gcr, begin, end);
        if(!gcr)
            return 0;
        gcr_data = wrap(&gcr, begin, end, 5, sector_buffer, 0);
    	conv_5bytes_gcr2bin(&gcr_data, &header[0]);
    	if((header[0] == 8)&&(header[2] == 0)) {
            // sector 0 found!
            offset = (gcr - begin) - 10;
            if(offset < 0)
                offset += tracks[track].track_length;
            return offset;
        }
    }
    return 0;    
}
    
bool GcrImage :: is_double_sided(void)
{
/*
    double_sided = false;

    // Check if there are any tracks defined for side 1
    for(int i=GCRIMAGE_FIRSTTRACKSIDE1; i < GCRIMAGE_MAXHDRTRACKS; i++) {
        if (tracks[i].track_address) {
            double_sided = true;
            break;
        }
    }
*/
    return double_sided;
}

bool GcrImage :: save(File *f, bool align, UserInterface *user_interface)
{
    uint8_t *header;
    int max_tracks;

    dump();

    if (double_sided) {
        max_tracks = GCRIMAGE_MAXHDRTRACKS;
        header = new uint8_t[16 + max_tracks * 8];
        memcpy(header, "GCR-1571", 8);
    } else {
        max_tracks = GCRIMAGE_FIRSTTRACKSIDE1;
        header = new uint8_t[16 + max_tracks * 8];
        memcpy(header, "GCR-1541", 8);
    }
    header[8] = 0;
    header[9] = (uint8_t)max_tracks;
    header[10] = (uint8_t)GCRIMAGE_MAXTRACKLEN;
    header[11] = (uint8_t)(GCRIMAGE_MAXTRACKLEN >> 8);

    uint32_t *pul = (uint32_t *)&header[12]; // because 12 is a multiple of 4, we can do this

    uint32_t track_start = 12 + max_tracks * 8;
    
    for(int i=0;i<max_tracks;i++) {
        if(!tracks[i].track_address || !tracks[i].track_used) {
            *(pul++) = 0;
        } else {
            *(pul++) = cpu_to_32le(track_start);
            track_start += tracks[i].track_length + 2;
        }
    }
    int speed;
    for(int i=0;i<max_tracks;i++) {
        //speed = region_speed_codes[track_to_region((i < GCRIMAGE_FIRSTTRACKSIDE1) ? i/2 : (i - GCRIMAGE_FIRSTTRACKSIDE1) / 2, false)];

        if(!tracks[i].track_address || !tracks[i].track_used) {
            *(pul++) = 0;
        } else {
            *(pul++) = cpu_to_32le(tracks[i].speed_zone);
        }
    }

    uint32_t bytes_written;
    FRESULT res;
    f->write(header, 12 + max_tracks * 8, &bytes_written);
    delete header;
    
    if(bytes_written != 12 + max_tracks * 8)
        return false;
        
    //uint8_t *filler_bytes = new uint8_t[C1541_MAXTRACKLEN];
    //memset(filler_bytes, 0xFF, C1541_MAXTRACKLEN);
    
    uint8_t size[2];
    int skipped = 0;
    for(int i=0;i<max_tracks;i++) {
        if(!tracks[i].track_address || !tracks[i].track_used) {
            skipped++;
            continue;
        }
        int reported_length = tracks[i].track_length;
        size[0] = (uint8_t)reported_length;
        size[1] = (uint8_t)(reported_length >> 8);
        if (tracks[i].track_is_mfm) {
            size[1] |= 0x80; // MFM flag
        }
        f->write(size, 2, &bytes_written);
        if(bytes_written != 2)
            break;
        // find alignment
        int start = 0;
        if(align)
            start = find_track_start(i);
        if(start > 0) {
            res = f->write(tracks[i].track_address+start, tracks[i].track_length-start, &bytes_written);
            if (res == FR_OK) {
                res = f->write(tracks[i].track_address, start, &bytes_written);
            }
        } else {
        	res = f->write(tracks[i].track_address, tracks[i].track_length, &bytes_written);
        }
        if(res != FR_OK)
            break;
        //res = f->write(filler_bytes, C1541_MAXTRACKLEN - track_length[i], &bytes_written);
        if(user_interface)
            user_interface->update_progress(NULL, 1 + skipped);
        if(res != FR_OK)
            break;
        skipped = 0;
    }
    
    //delete filler_bytes;
    
    if(res != FR_OK)
        return false;
    return true;
}
    
void GcrImage :: track_got_written_with_gcr(int track)
{
    if(!tracks[track].track_address)
        return;

    if (track >= GCRIMAGE_FIRSTTRACKSIDE1) {
        set_ds(true);
    }

    // There is currently no way to known at what speed the track got written; the hardware does not record it.
    // So we record the standard speed zone index here. This may overwrite the MFM code, and that is exactly why it is here; to
    // indicate that this track is now a GCR track.
    int speed = region_speed_codes[track_to_region((track < GCRIMAGE_FIRSTTRACKSIDE1) ? track/2 : (track - GCRIMAGE_FIRSTTRACKSIDE1) / 2, false)];
    tracks[track].speed_zone = speed;
    tracks[track].track_used = true;
    tracks[track].track_is_mfm = false;
}

bool GcrImage :: write_track(int track, File *f, bool align)
{
	if(!tracks[track].track_address) {
	    return false;
	}
	if(!tracks[track].in_image_file) {
        return false;
	}
	if (!f) {
	    return false;
    }
	uint32_t offset = uint32_t(tracks[track].track_address) - uint32_t(gcr_data);
	uint32_t bytes_written, bw2;

	if (offset < 2) {
	    return false;
	}
	FRESULT res = f->seek(offset-2); // write the length field as well, to update the mfm flag
	if(res != FR_OK) {
		return false;
	}

	uint8_t s[2];
	s[0] = (uint8_t)(tracks[track].track_length & 0xFF);
	s[1] = (uint8_t)(tracks[track].track_length >> 8);
	if (tracks[track].track_is_mfm) {
	    s[1] |= 0x80;
	}

	res = f->write(s, 2, &bytes_written);
	if (res != FR_OK) {
	    return false;
	}

	int start = 0;
    if(align)
        start = find_track_start(track);
    if(start > 0) {
    	res = f->write(tracks[track].track_address+start, tracks[track].track_length-start, &bytes_written);
        if(res != FR_OK)
    		return false;
        res = f->write(tracks[track].track_address, start, &bw2);
        bytes_written += bw2;
    } else {
    	res = f->write(tracks[track].track_address, tracks[track].track_length, &bytes_written);
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
    BinImage *bin = new BinImage("Test", 35);
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
    convert_disk_bin2gcr(bin, NULL, false);

    // now let's say we decode back to another location:
    bin->track_start[0] = bin->track_start[2];
    uint8_t *decoded = bin->track_start[2];

    // determine where to put the wrap byte
    uint8_t *gcr_next = tracks[0].track_address + tracks[0].track_length;
    
    printf("GCR Image ready to decode...\n");
    
    // now start decoding 600 times
    int total = 0;
    int dummy = 0;
    for(int i=0;i<600;i++) {
        // clear
        for(int j=0;j<bytes;j++)
            decoded[j] = 0;
        uint8_t status[32] = { 0 };

        // decode
        GcrImage :: convert_gcr_track_to_bin(gcr_data, 1, tracks[0].track_length, 21, decoded, status, 32);

        // compare (better than checking the return value)
        int errors = 0;
        for(int j=0;j<bytes;j++) {
            if (decoded[j] != bin_track0[j])
                errors ++;
        }
        printf("Decode position %d: Errors = %d\n", i, errors);
        total += errors;

        // shift up one byte
        *(gcr_next++) = *tracks[0].track_address;
        tracks[0].track_address ++;
    }
    printf("Test was %s\n", total?"NOT successful":"SUCCESSFUL!!");
    delete bin;
    return (total == 0);    
}
    
void GcrImage :: set_ds(bool ds)
{
    if (ds != double_sided) {
        printf("CHANGE OF DISK IMAGE: Image is now %s\n", ds ? "Double Sided" : "Single Sided");
    }
    double_sided = ds;
}

//--------------------------------------------------------------
// Binary Image Class
//--------------------------------------------------------------
BinImage ::BinImage(const char *name, int tracks)
{
    if (tracks <= 42) {
        double_sided = false;
    } else {
        double_sided = true;
    }
    int sects = 0;
    for (int i = 0; i < tracks; i++) {
        sects += sectors_per_track[track_to_region(i, double_sided)];
    }
    data_size = sects * 256;
    allocated_size = data_size + sects;
    bin_data = new uint8_t[allocated_size];
    uint8_t *track = bin_data;
    for (int i = 0; i < tracks; i++) {
        sects = sectors_per_track[track_to_region(i, double_sided)];
        track_start[i] = track;
        track_sectors[i] = sects;
        track += (256 * sects);
    }
    for (int i = tracks; i < BINIMAGE_MAXTRACKS; i++) {
        track_start[i] = NULL;
        track_sectors[i] = 0;
    }
    errors = track;
    error_size = sects;
    num_tracks = tracks;
    memset(errors, 0, error_size);
}

BinImage :: ~BinImage()
{
	if(bin_data)
		delete[] bin_data;
}


uint8_t * BinImage :: get_sector_pointer(int track, int sector)
{
	if (track < 1) {
		return 0;
	}
	if (sector < 0) {
		return 0;
	}
	if (track >= BINIMAGE_MAXTRACKS) {
		return 0;
	}
	if (sector >= track_sectors[track-1]) {
		return 0;
	}
    return track_start[track-1] + (sector << 8);
}

int BinImage :: copy(uint8_t *data, uint32_t size)
{
	if (size > allocated_size)
		size = allocated_size;
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
	res = file->read(bin_data, allocated_size, &transferred);
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
	double_sided = false;
	errors = NULL;
	if (size >= C1541_MIN_D71_SIZE) {
	    double_sided = true;
	    size -= C1541_MIN_D71_SIZE;
	    num_tracks = 70;
	    errors = &bin_data[C1541_MIN_D71_SIZE];
	    data_size = C1541_MIN_D71_SIZE;
	    error_size = size;
	} else { // single sided
        size -= C1541_MAX_D64_35_NO_ERRORS;
        num_tracks = 35;
        data_size = C1541_MAX_D64_35_NO_ERRORS;
        errors = &bin_data[C1541_MAX_D64_35_NO_ERRORS];
        while ((size >= 17*256) && (num_tracks < BINIMAGE_MAXTRACKS)) {
            num_tracks ++;
            size -= 17*256;
            errors += 17*256;
            data_size += 17*256;
        }
        error_size = (int)size;
	}
	if(size <= 0)
		errors = NULL;

    uint8_t *track = bin_data;
    for(int i=0;i<num_tracks;i++) {
        int region = track_to_region(i, double_sided);
        if (region >= 0) {
            int sects = sectors_per_track[region];
            track_start[i] = track;
            track_sectors[i] = sects;
            track += (256 * sects);
        } else {
            num_tracks = i;
            break;
        }
    }

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

	int secs = 0;
	for (int tr=0; tr < num_tracks; tr++) {
        uint8_t *data = track_start[tr];
        secs += track_sectors[tr];
        res = file->write(data, track_sectors[tr] * 256, &transferred);
        if(res != FR_OK) {
            printf("WRITE ERROR: %d. Transferred = %d\n", res, transferred);
            return -2;
        }
        if (user_interface) {
            user_interface->update_progress(NULL, 1);
        }
    }

	if(errors && error_size >= secs) {
	    uint8_t orred = 0;
	    for(int i=0;i<secs;i++) {
	       orred |= errors[i];
	    }
	    if (orred) { // contains valid error data
            res = file->write(errors, secs, &transferred);
            if(res != FR_OK) {
                printf("WRITE ERROR: %d. Transferred = %d\n", res, transferred);
                return -4;
            }
	    }
	}
	return 0;
}

int BinImage :: format(const char *name)
{
    memset(bin_data, 0, data_size);

    volatile int numBlocks = data_size / 256;
    // printf("NumBlocks = %d\n", numBlocks);
    BlockDevice_Ram *blk = new BlockDevice_Ram(bin_data, 256, numBlocks);
    Partition *prt = new Partition(blk, 0, numBlocks, 0);
    FileSystem *fs;
    if (double_sided && (data_size >= C1541_MIN_D71_SIZE)) {
        fs = new FileSystemD71(prt, true);
        num_tracks = 70;
    } else {
        fs = new FileSystemD64(prt, true);
        num_tracks = 35;
    }
    fs->format(name);

    delete fs;
    delete prt;
    delete blk;
    return 0;
}

int BinImage :: write_track(int phys_track, GcrImage *gcr_image, File *file, int& errorcount, int &secs)
{
	int logical_track = gcr_track_to_bin_track(phys_track);
	secs = 0;

	uint8_t *bin = track_start[logical_track];
	if (!bin) {
	    return -10; // track doesn't exist in binary image
    }
    GcrTrack *gcrTrack = &(gcr_image->tracks[phys_track]);
	uint8_t *gcr = gcrTrack->track_address;
	if (!gcr) {
	    return -11; // Track doesn't exist in the gcr image
	}
    uint8_t status[64];
    int expected_secs = track_sectors[logical_track];
    secs = GcrImage :: convert_gcr_track_to_bin(gcr, logical_track + 1, gcrTrack->track_length, expected_secs, bin, status, 64);

    // Store errors in error bytes
    int error_offset = 0;
    if (errors) { // are there error bytes?
        error_offset = total_sectors_before_track(logical_track, double_sided);
    }

    // Report
    printf("%d sectors found. (", secs);
    for(int i=0;i<2*secs;i+=2) {
        printf("%b:%b ", status[i], status[i+1]);
        if (errors) {
            if (error_offset + status[i] < error_size) {
                errors[error_offset + status[i]] = status[i+1];
            }
        }
        if (status[i+1]) {
            errorcount++;
        }
    } printf(")\n");

    if(secs != expected_secs) {
        printf("Decode failed.\n");
		return -12;
	}

    if (!file) {
        // No need to write to a file, just to the image
        return 0;
    }
    uint32_t offset = uint32_t(bin - bin_data);
	// Write
	FRESULT res = file->seek(offset);
	if(res != 0) {
        printf("While trying to write track %d, seek offset $%6x failed with error %d.\n", logical_track+1, offset, res);
		return res;
	}
	uint32_t transferred;
	FRESULT fres = file->write(bin, 256*track_sectors[logical_track], &transferred);
	if(fres != FR_OK)
		return res;
    return file->sync();
}

bool BinImage :: is_double_sided(void)
{
    return double_sided;
}

void BinImage :: get_sensible_name(char *buffer)
{
    buffer[0] = 0;
    Directory *r;

    BlockDevice_Ram *blk = new BlockDevice_Ram(bin_data, 256, 683);
    Partition *prt = new Partition(blk, 0, 683, 0);
    FileSystem *fs = new FileSystemD64(prt, false);

    if (fs->dir_open(NULL, &r) != FR_OK) {
        strcpy(buffer, "Unreadable.");
        return;
    }
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
    delete r;
    delete fs;
    delete prt;
    delete blk;
}

//BinImage static_bin_image("Static Binary Image"); // for general use

SubsysResultCode_e ImageCreator :: S_createD71(SubsysCommand *cmd)
{
    FileManager *fm = FileManager :: getFileManager();
    File *f = 0;
    uint32_t written;
    char name_buffer[32];
    name_buffer[0] = 0;
    SubsysResultCode_e result = SSRET_OK;
    FRESULT fres = create_user_file(cmd->user_interface, "Give name for new disk..", ".d71", cmd->path.c_str(), &f, name_buffer);
    if (fres == FR_OK) {
        fres = write_zeros(f, 256*683*2, written);
    } else {
        result = SSRET_CANNOT_OPEN_FILE;
    }
    if (fres == FR_OK) {
        fres = f->seek(0);
    }
    if (fres == FR_OK) {
        BlockDevice_File blk(f, 256);
        Partition prt(&blk, 0, 0, 0);
        FileSystemD71 fs(&prt, true);
        fs.format(name_buffer);
    }
    if (fres != FR_OK) {
        cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
    }
    if (f) {
        fm->fclose(f);
    }
    return (fres == FR_OK) ? SSRET_OK : SSRET_DISK_ERROR;
}

SubsysResultCode_e ImageCreator :: S_createD81_81(SubsysCommand *cmd)
{
    FileManager *fm = FileManager :: getFileManager();
    File *f = 0;
    uint32_t written;
    char name_buffer[32];
    name_buffer[0] = 0;
    FRESULT fres = create_user_file(cmd->user_interface, "Give name for new disk..", ".d81", cmd->path.c_str(), &f, name_buffer);
    if (fres == FR_OK) {
        fres = write_zeros(f, 81*10240, written);
    }
    if (fres == FR_OK) {
        fres = f->seek(0);
    }
    if (fres == FR_OK) {
        BlockDevice_File blk(f, 256);
        Partition prt(&blk, 0, 0, 0);
        FileSystemD81 fs(&prt, true);
        fs.format(name_buffer);
    }
    if (fres != FR_OK) {
        cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
    }
    if (f) {
        fm->fclose(f);
    }
    return (fres == FR_OK) ? SSRET_OK : SSRET_DISK_ERROR;
}

SubsysResultCode_e ImageCreator :: S_createD81(SubsysCommand *cmd)
{
    FileManager *fm = FileManager :: getFileManager();
    File *f = 0;
    uint32_t written;
    char name_buffer[32];
    name_buffer[0] = 0;
    FRESULT fres = create_user_file(cmd->user_interface, "Give name for new disk..", ".d81", cmd->path.c_str(), &f, name_buffer);
    if (fres == FR_OK) {
        fres = write_zeros(f, 256*3200, written);
    }
    if (fres == FR_OK) {
        fres = f->seek(0);
    }
    if (fres == FR_OK) {
        BlockDevice_File blk(f, 256);
        Partition prt(&blk, 0, 0, 0);
        FileSystemD81 fs(&prt, true);
        fs.format(name_buffer);
    }
    if (fres != FR_OK) {
        cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
    }
    if (f) {
        fm->fclose(f);
    }
    return (fres == FR_OK) ? SSRET_OK : SSRET_DISK_ERROR;
}

SubsysResultCode_e ImageCreator :: S_createDNP(SubsysCommand *cmd)
{
    FileManager *fm = FileManager :: getFileManager();
    File *f = 0;
    uint32_t written;
    char name_buffer[32];
    char size_buffer[16];
    name_buffer[0] = 0;
    size_buffer[0] = 0;
    int tracks = 0;
    FRESULT fres = create_user_file(cmd->user_interface, "Give name for disk image..", ".dnp", cmd->path.c_str(), &f, name_buffer);
    if (fres == FR_OK) {
        if (cmd->user_interface->string_box("Give size in tracks..", size_buffer, 16) <= 0) {
            return SSRET_INVALID_PARAMETER;
        }
        sscanf(size_buffer, "%d", &tracks);
        if (tracks < 1) {
            cmd->user_interface->popup("Should be at least 1 track", BUTTON_OK);
            return SSRET_INVALID_PARAMETER;
        }
        if (tracks >= 256) {
            cmd->user_interface->popup("Should be less than 256 tracks", BUTTON_OK);
            return SSRET_INVALID_PARAMETER;
        }
    }
    if (fres == FR_OK) {
        fres = write_zeros(f, 65536*tracks, written);
    }
    if (fres == FR_OK) {
        fres = f->seek(0);
    }
    if (fres == FR_OK) {
        BlockDevice_File blk(f, 256);
        Partition prt(&blk, 0, 0, 0);
        FileSystemDNP fs(&prt, true);
        fs.format(name_buffer);
    }
    if (fres != FR_OK) {
        cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
    }
    if (f) {
        fm->fclose(f);
    }
    return (fres == FR_OK) ? SSRET_OK : SSRET_DISK_ERROR;
}

SubsysResultCode_e ImageCreator :: S_createD64(SubsysCommand *cmd)
{
	int doGCR = (cmd->mode & 1);
	int doDS  = (cmd->mode & 2);

	const int tracks[] = { 35, 40, 70, 80 };
	const char *extensions[] = { ".d64", ".g64", ".d71", ".g71" };
	char buffer[64];

	buffer[0] = 0;
	int res;
	BinImage *bin;
	GcrImage *gcr;
	FileManager *fm = FileManager :: getFileManager();
	bool save_result;
	SubsysResultCode_e retval = SSRET_OK;

	res = cmd->user_interface->string_box("Give name for new disk..", buffer, 22);
	if(res > 0) {
	    bin = new BinImage("Temporary Binary Image", tracks[cmd->mode]);
		if(bin) {
    		bin->format(buffer);
            fix_filename(buffer);
			set_extension(buffer, extensions[cmd->mode], 32);
            File *f = 0;
            FRESULT fres = fm -> fopen(cmd->path.c_str(), buffer, FA_WRITE | FA_CREATE_NEW, &f);
			if(f) {
                if(doGCR) {
                    gcr = new GcrImage;
                    if(gcr) {
                        cmd->user_interface->show_progress("Converting..", tracks[cmd->mode]);
                        gcr->convert_disk_bin2gcr(bin, cmd->user_interface, 0);
                        cmd->user_interface->update_progress("Saving...", 1);
                        save_result = gcr->save(f, false, cmd->user_interface); // create image, without alignment, we are aligned already
                        cmd->user_interface->hide_progress();
                        delete gcr;
                    } else {
                        printf("No memory to create gcr image.\n");
                        retval = SSRET_OUT_OF_MEMORY;
                    }
                } else {
                    cmd->user_interface->show_progress("Creating...", 100);
                    save_result = bin->save(f, cmd->user_interface);
                    cmd->user_interface->hide_progress();
                }
        		printf("Result of save: %d.\n", save_result);
                fm->fclose(f);
			} else {
				printf("Can't create file '%s'\n", buffer);
				cmd->user_interface->popup(FileSystem :: get_error_string(fres), BUTTON_OK);
				retval = SSRET_SAVE_FAILED;
			}
			delete bin;
		} else {
			printf("No memory to create bin.\n");
			retval = SSRET_OUT_OF_MEMORY;
		}
	}
	return retval;
}

// instantiate so that we exist
ImageCreator image_creator;
