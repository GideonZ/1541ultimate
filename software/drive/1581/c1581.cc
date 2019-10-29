#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>

#include "itu.h"
#include "dump_hex.h"
#include "c64.h"
#include "userinterface.h"
#include "filemanager.h"
#include "iec.h"

#include "c1581.h"
#include "c1581_channel.h"
#include "../../io/iec/iec.h"


#define MAX_TRACK       80
#define MAX_SECTOR		39
#define MAX_DIR_ENTRIES	296
#define CMD_CHANNEL		15

const char *en_dis_81[] = { "Disabled", "Enabled" };
const char *yes_no_81[] = { "No", "Yes" };

#define CFG_C1581_POWERED   0xD1
#define CFG_C1581_BUS_ID    0xD2

#define CFG_C1541_RAMBOARD  0xD5
#define CFG_C1541_SWAPDELAY 0xD6
#define CFG_C1541_LASTMOUNT 0xD7
#define CFG_C1541_C64RESET  0xD8
#define CFG_C1541_GCRALIGN  0xDA
#define CFG_C1541_STOPFREEZ 0xDB

#define BASIC_START			0x0801
#define MAX_FILE_COUNT		296

struct t_cfg_definition c1581_config[] = {
    { CFG_C1581_POWERED,   CFG_TYPE_ENUM,   "1581 Drive",                 "%s", en_dis_81,     0,  1, 1 },
    { CFG_C1581_BUS_ID,    CFG_TYPE_VALUE,  "1581 Drive Bus ID",          "%d", NULL,       8, 11, 10 },
    { 0xFF, CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

uint32_t trackOffset[80] = {
		0x00000,0x02800,0x05000,0x07800,0x0A000,0x0C800,0x0F000,0x11800,0x14000,0x16800,0x19000,0x1B800,0x1E000,0x20800,0x23000,0x25800,
		0x28000,0x2A800,0x2D000,0x2F800,0x32000,0x34800,0x37000,0x39800,0x3C000,0x3E800,0x41000,0x43800,0x46000,0x48800,0x4B000,0x4D800,
		0x50000,0x52800,0x55000,0x57800,0x5A000,0x5C800,0x5F000,0x61800,0x64000,0x66800,0x69000,0x6B800,0x6E000,0x70800,0x73000,0x75800,
		0x78000,0x7A800,0x7D000,0x7F800,0x82000,0x84800,0x87000,0x89800,0x8C000,0x8E800,0x91000,0x93800,0x96000,0x98800,0x9B000,0x9D800,
		0xA0000,0xA2B00,0xA5000,0xA7800,0xAA000,0xAC800,0xAF000,0xB1800,0xB4000,0xB6800,0xB9000,0xBB800,0xBE000,0xC0800,0xC3000,0xC5800
};


const char msg00[] = " OK";						//00
const char msg01[] = " FILES SCRATCHED";		//01	Track number shows how many files were removed
const char msg02[] = "PARTITION SELECTED";      //02
const char msg20[] = "READ ERROR"; 				//20 (Block Header Not Found)
//const char msg21[] = "READ ERROR"; 				//21 (No Sync Character)
//const char msg22[] = "READ ERROR"; 				//22 (Data Block not Present)
//const char msg23[] = "READ ERROR"; 				//23 (Checksum Error in Data Block)
//const char msg24[] = "READ ERROR"; 				//24 (Byte Decoding Error)
const char msg25[] = "WRITE ERROR";				//25 (Write/Verify Error)
const char msg26[] = "WRITE PROTECT ON";		//26
//const char msg27[] = "READ ERROR"; 				//27 (Checksum Error in Header)
//const char msg28[] = "WRITE ERROR"; 			//28 (Long Data Block)
const char msg29[] = "DISK ID MISMATCH";		//29
const char msg30[] = "SYNTAX ERROR";			//30 (General)
const char msg31[] = "SYNTAX ERROR";			//31 (Invalid Command)
const char msg32[] = "SYNTAX ERROR";			//32 (Command Line > 58 Characters)
const char msg33[] = "SYNTAX ERROR";			//33 (Invalid Filename)
const char msg34[] = "SYNTAX ERROR";			//34 (No File Given)
const char msg39[] = "SYNTAX ERROR";			//39 (Invalid Command)
const char msg50[] = "RECORD NOT PRESENT";		//50
const char msg51[] = "OVERFLOW IN RECORD";		//51
//const char msg52[] = "FILE TOO LARGE";			//52
const char msg60[] = "WRITE FILE OPEN";			//60
const char msg61[] = "FILE NOT OPEN";			//61
const char msg62[] = "FILE NOT FOUND";			//62
const char msg63[] = "FILE EXISTS";				//63
const char msg64[] = "FILE TYPE MISMATCH";		//64
const char msg65[] = "NO BLOCK";				//65
const char msg66[] = "ILLEGAL TRACK AND SECTOR";//66
//const char msg67[] = "ILLEGAL SYSTEM T OR S";	//67
const char msg69[] = "FILESYSTEM ERROR";        //69
const char msg70[] = "NO CHANNEL";	            //70
const char msg71[] = "DIRECTORY ERROR";			//71
const char msg72[] = "DISK FULL";				//72
const char msg73[] = "COPYRIGHT CBM DOS V10 1581";	//73 DOS MISMATCH(Returns DOS Version)
const char msg74[] = "DRIVE NOT READY";			//74
const char msg77[] = "SELECTED PARTITION ILLEGAL"; //77
const char msg_c1[] = "BAD COMMAND";			//custom
const char msg_c2[] = "UNIMPLEMENTED";			//custom

const IEC_ERROR_MSG last_error_msgs[] = {
		{ 00,(char*)msg00,NR_OF_EL(msg00) - 1 },
		{ 01,(char*)msg01,NR_OF_EL(msg01) - 1 },
        { 02,(char*)msg02,NR_OF_EL(msg02) - 1 },
		{ 20,(char*)msg20,NR_OF_EL(msg20) - 1 },
//		{ 21,(char*)msg21,NR_OF_EL(msg21) - 1 },
//		{ 22,(char*)msg22,NR_OF_EL(msg22) - 1 },
//		{ 23,(char*)msg23,NR_OF_EL(msg23) - 1 },
//		{ 24,(char*)msg24,NR_OF_EL(msg24) - 1 },
		{ 25,(char*)msg25,NR_OF_EL(msg25) - 1 },
		{ 26,(char*)msg26,NR_OF_EL(msg26) - 1 },
//		{ 27,(char*)msg27,NR_OF_EL(msg27) - 1 },
//		{ 28,(char*)msg28,NR_OF_EL(msg28) - 1 },
		{ 29,(char*)msg29,NR_OF_EL(msg29) - 1 },
		{ 30,(char*)msg30,NR_OF_EL(msg30) - 1 },
		{ 31,(char*)msg31,NR_OF_EL(msg31) - 1 },
		{ 32,(char*)msg32,NR_OF_EL(msg32) - 1 },
		{ 33,(char*)msg33,NR_OF_EL(msg33) - 1 },
		{ 34,(char*)msg34,NR_OF_EL(msg34) - 1 },
		{ 39,(char*)msg39,NR_OF_EL(msg39) - 1 },
		{ 50,(char*)msg50,NR_OF_EL(msg50) - 1 },
		{ 51,(char*)msg51,NR_OF_EL(msg51) - 1 },
//		{ 52,(char*)msg52,NR_OF_EL(msg52) - 1 },
		{ 60,(char*)msg60,NR_OF_EL(msg60) - 1 },
		{ 61,(char*)msg61,NR_OF_EL(msg61) - 1 },
		{ 62,(char*)msg62,NR_OF_EL(msg62) - 1 },
		{ 63,(char*)msg63,NR_OF_EL(msg63) - 1 },
		{ 64,(char*)msg64,NR_OF_EL(msg64) - 1 },
		{ 65,(char*)msg65,NR_OF_EL(msg65) - 1 },
		{ 66,(char*)msg66,NR_OF_EL(msg66) - 1 },
//		{ 67,(char*)msg67,NR_OF_EL(msg67) - 1 },
        { 69,(char*)msg69,NR_OF_EL(msg69) - 1 },
		{ 70,(char*)msg70,NR_OF_EL(msg70) - 1 },
		{ 71,(char*)msg71,NR_OF_EL(msg71) - 1 },
		{ 72,(char*)msg72,NR_OF_EL(msg72) - 1 },
		{ 73,(char*)msg73,NR_OF_EL(msg73) - 1 },
		{ 74,(char*)msg74,NR_OF_EL(msg74) - 1 },
        { 77,(char*)msg77,NR_OF_EL(msg77) - 1 },

		{ 75,(char*)msg_c1,NR_OF_EL(msg_c1) - 1 },
		{ 76,(char*)msg_c2,NR_OF_EL(msg_c2) - 1 }
};


BamAllocation alloc;

C1581 ::C1581(char letter) : SubSystem(SUBSYSID_DRIVE_C)
{
	fm = FileManager :: getFileManager();

	//registers  = regs;
	drive_name = "Drive ";
	drive_letter = letter;
	//mount_file = NULL;
	iec_address = 8 + int(letter - 'A');
	last_command = 0;
	nxtsector = 0;
	nxttrack = 0;
	prevtrack = 0;
	prevsector = 0;
	cursector = 0;
	curtrack=0;
	bamisdirty=false;
	disk_state = e_no_disk81;
	sectorBuffer = 0;
	last_error = ERR_DOS;
	curbamtrack = 40;
	curbamsector = 1;
	startingDirTrack = 40;
	startingDirSector = 3;

	char buffer[32];
	sprintf(buffer, "1581 Drive %c Settings", letter);    
    //register_store((uint32_t)regs, buffer, c1581_config);
}
C1581 ::~C1581()
{
}

int C1581 :: executeCommand(SubsysCommand *cmd)
{
	File *file;
	FileInfo info(32);

	switch(cmd->functionID) {
		case D81FILE_MOUNT:
		{
			FRESULT res = fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);
			if(file == NULL)
				return -1;

			strcpy(mounted_path, cmd->path.c_str());
			strcpy(mounted_filename, cmd->filename.c_str());

			mount_d81(false, file);

			//c64->unfreeze();
			SubsysCommand *c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64,C64_UNFREEZE, 0, "", "");
			c64_command->execute();
		}
		break;
		default:
			printf("Unhandled menu item for C1581.\n");
			return -1;
	}
	return 0;
}

void C1581 :: init()
{
	for (int x = 0; x < CMD_CHANNEL; x++)
	{
		channels[x] = new C1581_Channel();
		channels[x]->init(this, x);
	}
	channels[15] = new C1581_CommandChannel();
	channels[15]->init(this, CMD_CHANNEL);
	memset(mount_file, 0, DISK_SIZE);
}

void C1581 :: unlink(void)
{
	disk_state = e_disk81_file_closed;
	write_d81();

	//if(mount_file) {
	//	mount_file = NULL;
	//}
}

// Disk Image Functions
int C1581 :: mount_d81(bool protect, File *file)
{
	uint32_t transferred = 0;

	printf("Loading...");
	FRESULT res = file->seek(0);
	if(res != FR_OK)
		return -1;

	int sz = file->get_size();
	if(sz != DISK_SIZE)
		return -3;

	//mount_file = (int *)malloc(DISK_SIZE);

	res = file->read(mount_file, DISK_SIZE, &transferred);
	if(res != FR_OK)
		return -2;

	curbamtrack = 40;
	curbamsector = 1;
	curtrack = curbamtrack;
	cursector = 0;
	startingDirTrack = curbamtrack;
	startingDirSector = 3;

	readBAMtocache();

	printf("Transferred: %d bytes\n", transferred);
	printf("Done\n");
	disk_state = e_d81_disk;

	return 0;
}

void C1581 :: mount_blank()
{

}

void C1581 :: getcurrentsector(uint8_t *buf)
{
	memcpy(buf, this->sectorBuffer, BLOCK_SIZE);
	return;
}

uint8_t C1581::goTrackSector(uint8_t track, uint8_t sector)
{
	if (track < 1 || track > MAX_TRACK || sector < 0 || sector > MAX_SECTOR)
	{
		return -1;
	}

	prevtrack = curtrack;
	prevsector = cursector;

	curtrack = track;
	cursector = sector;

	nxttrack = 0;
	nxtsector = 0;

	uint32_t offset = trackOffset[curtrack-1];
	offset = offset + cursector * BLOCK_SIZE;
	
	sectorBuffer = &(mount_file[offset]);
	return 0;
}

int C1581::bufferRead(uint8_t track, uint8_t sector, uint8_t buffer)
{
	goTrackSector(track, sector);

	for(int x=0; x<BLOCK_SIZE; x++)
		buffers[buffer][x] = sectorBuffer[x];

	return ERR_OK;
}

void C1581::writeSector(void)
{
	write_d81();
	return;
}

void C1581::write_d81(void)
{
	File *file;
	char fullpath[512];
	uint32_t bytesWritten;

	strcpy(fullpath, mounted_path);
	strcat(fullpath, mounted_filename);

	FRESULT res = fm->fopen(mounted_path, mounted_filename, FA_CREATE_ALWAYS | FA_WRITE, &file);
	file->write(mount_file, (uint32_t)DISK_SIZE, &bytesWritten);
	fm->fclose(file);
}

// BAM Functions
int C1581::readBAMtocache(void)
{
	uint8_t tmptrack = curtrack;
	uint8_t tmpsector = cursector;

	goTrackSector(curbamtrack,curbamsector);
	memcpy(BAMCache1, sectorBuffer, BLOCK_SIZE);

	goTrackSector(curbamtrack,curbamsector+1);
	memcpy(BAMCache2, sectorBuffer, BLOCK_SIZE);

	goTrackSector(tmptrack, tmpsector);
	return IEC_OK;
}

int C1581::writeBAMfromcache(void)
{
	uint8_t tmptrack = curtrack;
	uint8_t tmpsector = cursector;

	goTrackSector(curbamtrack,curbamsector);
	memcpy(sectorBuffer, BAMCache1, BLOCK_SIZE);

	goTrackSector(curbamtrack,curbamsector+1);
	memcpy(sectorBuffer, BAMCache2, BLOCK_SIZE);
	writeSector();

	goTrackSector(tmptrack, tmpsector);

	return IEC_OK;
}

int C1581::findFreeSector(bool file, uint8_t *track, uint8_t *sector)
{
	static int direction = 1;

	if(file)
	{
		if(curbamtrack == 40)
			direction = -direction;
		else
			direction = 1;
	}

	alloc.track = curbamtrack + direction;
	alloc.sector = 0;

	uint8_t *bamdata = (direction == -1 ? BAMCache1 : BAMCache2);
	int offset = (direction == -1 ? 0xF4 : 0x10);

	while (1)
	{
		// first byte represents number of free sectors on track
		if (bamdata[offset] > 0)
		{
			// next 5 bytes are sector allocation for the track in reverse
			for (int z = 0; z < 5; z++)
			{			
				uint8_t bit = 128;	
				uint8_t b = bamdata[++offset];

				// reverse the bits (makes the math easier)
				b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
				b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
				b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
					
				do {
					// locate the free track / sector
					if ((b & bit) == bit)
					{
						*track = alloc.track;
						*sector = alloc.sector;
						return ERR_OK;
					}

					// 'bit' goes from 128, 64, 32, 16, 8, 4 , 2 ,1
					// to check each bit. 1 = free, 0 = allocated
					bit = bit / 2;	
					alloc.sector++;

				} while (bit >= 1);
			
			}
			// we should not be here if the BAM sector
			// count byte is accurate
		}
		else
		{
			// no free sectors. proceed to next track
			alloc.track += direction;
			alloc.sector = 0;
			offset += (6 * direction);
		}

		if (alloc.track < 1 || alloc.track > 80)
			return ERR_DISK_FULL;
	}

	return ERR_OK;
}

bool C1581::getTrackSectorAllocation(uint8_t track, uint8_t sector)
{
	uint8_t t = 0;
	uint8_t *bamdata;

	if(track <= 40)
	{
		// position at the track BAM allocation record (side 0)
		bamdata = BAMCache1 + 0x10 + ((track-1)*6);
	}
	else
	{
		// position at the track BAM allocation record (side 1)
		bamdata = BAMCache2 + 0x10 + ((track-41)*6);
	}

	uint8_t s;
	if(sector < 8)
		s = 1;
	else if (sector < 16)
		s = 2;
	else if (sector < 24)
		s = 3;
	else if (sector < 32)
		s = 4;
	else if (sector < 40)
		s = 5;

	uint8_t bit = (sector % 8);
	uint8_t bits[] = { 1,2,4,8,16,32,64,128};


	if((bamdata[s] & bits[bit]) == 0)
		return true;
	else
		return false;
}

int C1581::setTrackSectorAllocation(uint8_t track, uint8_t sector, bool allocate)
{
	uint8_t t = 0;
	uint8_t *bamdata;

	if(track <= 40)
	{
		// position at the track BAM allocation record (side 0)
		bamdata = BAMCache1 + 0x10 + ((track-1)*6);
	}
	else
	{
		// position at the track BAM allocation record (side 1)
		bamdata = BAMCache2 + 0x10 + ((track-41)*6);
	}

	uint8_t s;
	if(sector < 8)
		s = 1;
	else if (sector < 16)
		s = 2;
	else if (sector < 24)
		s = 3;
	else if (sector < 32)
		s = 4;
	else if (sector < 40)
		s = 5;

	uint8_t bit = (sector % 8) + 1;

	if(allocate == true)
	{
		// allocate the sector
		bamdata[s] = (bamdata[s] & ~(1 << (bit - 1))); // turn off bit
		bamdata[0]--; // free sectors -1
	}
	else
	{
		// deallocate the sector
		bamdata[s] = (bamdata[s] | (1 << (bit - 1))); // turn on bit
		bamdata[0]++; // free sectors + 1
	}

	writeBAMfromcache();
	return IEC_OK;
}

int C1581::getFileTrackSector(char *filename, uint8_t *track, uint8_t *sector, bool deleted)
{
	static bool firstcall = true;
	static int dirctr = 0;
	static bool lastdirsector = false;
	int ptr = 0;
	uint8_t linePtr = 0;
	DirectoryEntry *dirEntry = new DirectoryEntry;
	int dirptr = 0;

	int x = 0;

	dirptr = getNextDirectoryEntry(&firstcall, &dirctr, &lastdirsector, dirEntry);

	while (dirptr != -1)
	{
		if((deleted == false && dirEntry->file_type != 0) || deleted == true)
		{
			char line[17];
			uint8_t z = 0;
			for(; z < 16; z++)
			{
				if (dirEntry->filename[z] == 0xa0)
					break;

				line[z] = dirEntry->filename[z];
			}

			line[z] = 0;

			if (strcmp(line, filename) == 0)
			{
				*track = dirEntry->first_data_track;
				*sector = dirEntry->first_data_sector;
				firstcall = true;
				dirctr = 0;
				lastdirsector = false;
				delete dirEntry;
				return ERR_OK;
			}
		}
		dirptr = getNextDirectoryEntry(&firstcall, &dirctr, &lastdirsector, dirEntry);
	}

	firstcall = true;
	dirctr = 0;
	lastdirsector = false;
	delete dirEntry;
	return ERR_FILE_NOT_FOUND;
}

// Directory Functions
int C1581::get_directory(uint8_t *buffer)
{
	static bool firstcall = true;
	static int dirctr = 0;
	static bool lastdirsector = false;
	DirectoryEntry *dirEntry = new DirectoryEntry;
	int ptr = 0;
	uint16_t blocksfree = 0;
	uint16_t nextLinePtr = 0;

	uint8_t filetypes[6][3] = {
			{ 'D', 'E', 'L' },
			{ 'S', 'E', 'Q' },
			{ 'P', 'R', 'G' },
			{ 'U', 'S', 'R' },
			{ 'R', 'E', 'L' },
			{ 'C', 'B', 'M' }
	};

	goTrackSector(curbamtrack, 0);

	// best way i could find to check
	// if the subdir has been formatted
	// 1st byte of BAM should always be
	// the curent BAM track.
	if(sectorBuffer[0] != curbamtrack)
		return -1;


	buffer[ptr++] = 0x01;
	buffer[ptr++] = 0x08;

	nextLinePtr = ptr;
	ptr += 2;

	// disk header
	sprintf(((char *)buffer + ptr), "%c%c%c%c", 0x00, 0x00, 0x12, 0x22);
	ptr += 4;

	for (int t = 0x04; t < 0x14; t++)
	{
		sprintf((char *)buffer + ptr, "%c", sectorBuffer[t]);
		ptr++;
	}

	uint8_t diskid1 = sectorBuffer[0x16] & 0x7F;
	uint8_t diskid2 = sectorBuffer[0x17] & 0x7F;

	sprintf(((char *)buffer + ptr), "%c %c%c %c%c\0", 0x22, diskid1, diskid2, sectorBuffer[0x19], sectorBuffer[0x1A]);
	ptr += 8;

	// testing with normal C64 line links
	buffer[nextLinePtr] = (BASIC_START + ptr-2) & 0xff;
	buffer[nextLinePtr + 1] = ((BASIC_START + ptr-2) >> 8) & 0xff;

	// actual data has no line links (so that it can be linked by other cbm machines)
	//buffer[nextLinePtr] = 0x0101 & 0xff;
	//buffer[nextLinePtr + 1] = (0x0101 >> 8) & 0xff;

	int x = getNextDirectoryEntry(&firstcall, &dirctr, &lastdirsector, dirEntry);
	char line[30];
	int linePtr = 0;
	int ctr = 0;
	while (x != -1)
	{
		if (dirEntry->file_type != 0)
		{
			ctr++;
			linePtr = 0;

			nextLinePtr = ptr;
			ptr += 2;

			line[linePtr++] = dirEntry->size_lo;
			line[linePtr++] = dirEntry->size_hi;

			int blocks = dirEntry->size_hi * 256 + dirEntry->size_lo;
			int digits = 1;
			while (blocks >= 0)
			{
				blocks /= 10;
				++digits;

				if (blocks == 0)
					break;
			}
			int spaces = 5 - digits;
			for (; spaces > 0; spaces--)
				line[linePtr++] = ' ';

			line[linePtr++] = 0x22;

			int filenamlen = 0;
			for (; filenamlen < 16; filenamlen++)
			{
				if (dirEntry->filename[filenamlen] == 0xa0)
					break;

				line[linePtr++] = dirEntry->filename[filenamlen];
			}

			line[linePtr++] = 0x22;

			for (; filenamlen < 16; filenamlen++)
				line[linePtr++] = ' ';

			uint8_t tmptype = dirEntry->file_type;
			tmptype = tmptype & ~128;
			tmptype = tmptype & ~64;
			tmptype = tmptype & ~32;
			tmptype = tmptype & ~16;

			if ((dirEntry->file_type & 0x80) != 0x80)
				line[linePtr++] = '*';
			else
				line[linePtr++] = ' ';

			line[linePtr++] = filetypes[tmptype][0];
			line[linePtr++] = filetypes[tmptype][1];
			line[linePtr++] = filetypes[tmptype][2];

			if ((dirEntry->file_type & 64) == 64)
				line[linePtr++] = '<';
			else
				line[linePtr++] = ' ';

			line[linePtr++] = 0;

			if (linePtr > 30)
			{
				abort();
			}

			for (int x = 0; x < linePtr; x++)
				buffer[ptr++] = line[x];

			buffer[nextLinePtr] = (BASIC_START + ptr-2) & 0xff;
			buffer[nextLinePtr + 1] = ((BASIC_START + ptr-2) >> 8) & 0xff;
		}

		x = getNextDirectoryEntry(&firstcall, &dirctr, &lastdirsector, dirEntry);
	}


	nextLinePtr = ptr;
	ptr += 2;

	blocksfree = getBlocksFree();

	sprintf(((char *)buffer + ptr), "%c%cBLOCKS FREE.              ", blocksfree % 256, blocksfree / 256);
	ptr += 29;

	buffer[nextLinePtr] = (0x0801 + ptr) & 0xff;
	buffer[nextLinePtr + 1] = ((0x0801 + ptr) >> 8) & 0xff;

	// end of program
	buffer[ptr] = 0;
	buffer[ptr + 1] = 0;
	buffer[ptr + 2] = 0;
	buffer[ptr + 3] = 0;
	ptr += 4;

	firstcall = true;
	dirctr = 0;
	lastdirsector = 0;

	delete dirEntry;
	return ptr;
}

int C1581::getNextDirectoryEntry(bool *firstcall, int *dirctr, bool *lastdirsector, DirectoryEntry *dirEntry)
{
	bool readnextsector = false;

	int offset = 0;

	if (*firstcall == true)
	{
		*firstcall = false;
		goTrackSector(startingDirTrack, startingDirSector);
		nxttrack = sectorBuffer[0x00];
		nxtsector = sectorBuffer[0x01];
		*dirctr = 0;
	}
	else
	{
		if ((*dirctr > 0) && (*dirctr % 8 == 0))
		{
			if (*lastdirsector)
				return -1;

			if(goTrackSector(nxttrack, nxtsector) == 0)
			{
				nxttrack = sectorBuffer[0x00];
				nxtsector = sectorBuffer[0x01];
				*dirctr = 0;
			}
			else
			{
				get_last_error(last_error_msgs[20].msg,nxttrack,nxtsector);
				return -1;
			}

		}
	}

	offset = (*dirctr) * 32;

	if (nxttrack == 0x00 && nxtsector == 0xff)
		*lastdirsector = true;
	
	dirEntry->file_type = sectorBuffer[offset + 0x02];
	dirEntry->first_data_track = sectorBuffer[offset + 0x03];
	dirEntry->first_data_sector = sectorBuffer[offset + 0x04];

	for (int v = 0; v < 16; v++)
		dirEntry->filename[v] = sectorBuffer[offset + 0x05 + v];

	dirEntry->first_track_ssb = sectorBuffer[offset + 0x15];
	dirEntry->first_sector_ssb = sectorBuffer[offset + 0x16];
	dirEntry->rel_file_length = sectorBuffer[offset + 0x17];

	for (int v = 0; v < 6; v++)
		dirEntry->unused[v + *dirctr] = sectorBuffer[offset + 0x18 + v];

	dirEntry->size_lo = sectorBuffer[offset + 0x1e];
	dirEntry->size_hi = sectorBuffer[offset + 0x1f];

	(*dirctr)++;

	return offset;

}

int C1581::createDirectoryEntry(char *filename, uint8_t filetype, uint8_t *track, uint8_t *sector)
{
	bool firstcall = true;
	int dirctr = 0;
	bool lastdirsector = false;
	DirectoryEntry *dirEntry = new DirectoryEntry;
	int dirptr;
	int filecount = 0;

	while(filecount < MAX_FILE_COUNT)
	{
		dirptr = getNextDirectoryEntry(&firstcall, &dirctr, &lastdirsector, dirEntry);

		while (dirptr != -1)
		{
			filecount++;

			// if a deleted or empty entry, we can use this
			if(dirEntry->file_type == 0x00)
			{
				// if not CBM file type (they have already been allocated)
				// allocate a starting sector for the file
				if(filetype != 0x85)
				{
					int err = findFreeSector(true, track, sector);
					if(err != ERR_OK)
					{
						delete dirEntry;
						return err;
					}

					// allocate the sector
					err = setTrackSectorAllocation(*track, *sector, true);
					if(err != ERR_OK)
					{
						delete dirEntry;
						return err;
					}
				}

				// the 1st dir entry has next dir track/sector
				// zeros for the other seven
				if(dirctr == 1 && lastdirsector == true)
				{
					sectorBuffer[dirptr + 0x00] = 0x00;
					sectorBuffer[dirptr + 0x01] = 0xff;
				}
				else if(dirctr > 1)
				{
					sectorBuffer[dirptr + 0x00] = 0x00;
					sectorBuffer[dirptr + 0x01] = 0x00;
				}

				sectorBuffer[dirptr + 0x02] = filetype;
				sectorBuffer[dirptr + 0x03] = *track;
				sectorBuffer[dirptr + 0x04] = *sector;

				int v = 0;
				for (; v < strlen(filename); v++)
					sectorBuffer[dirptr + 0x05 + v] = filename[v];

				for (; v < 16; v++)
					sectorBuffer[dirptr + 0x05 + v] = 160;

				sectorBuffer[dirptr + 0x15] = 0;
				sectorBuffer[dirptr + 0x16] = 0;
				sectorBuffer[dirptr + 0x17] = 0;

				for (int v = 0; v < 6; v++)
					sectorBuffer[dirptr + 0x18 + v] = 0;

				sectorBuffer[dirptr + 0x1e] = 0;
				sectorBuffer[dirptr + 0x1f] = 0;

				// write the data back
				writeSector();
				delete dirEntry;
				return ERR_OK;
			}

			dirptr = getNextDirectoryEntry(&firstcall, &dirctr, &lastdirsector, dirEntry);
		}

		// need to allocate a new directory sector
		setTrackSectorAllocation(curtrack, cursector+1, true);
		sectorBuffer[0] = curtrack;			// link the current directory
		sectorBuffer[1] = cursector + 1;	// sector to the new one
		writeSector();						// write the current dir sector back

		// if not CBM file type (they have already been allocated)
		// allocate a starting sector for the file
		if(filetype != 0x85)
		{
			int err = findFreeSector(true, track, sector);
			if(err != ERR_OK)
			{
				delete dirEntry;
				return err;
			}

			// allocate the sector
			err = setTrackSectorAllocation(*track, *sector, true);
			if(err != ERR_OK)
			{
				delete dirEntry;
				return err;
			}
		}

		// add the new directory entry to new sector
		goTrackSector(curtrack, cursector + 1);
		sectorBuffer[0x00] = 0x00;
		sectorBuffer[0x01] = 0xff;
		sectorBuffer[0x02] = filetype;
		sectorBuffer[0x03] = *track;
		sectorBuffer[0x04] = *sector;

		int v = 0;
		for (; v < strlen(filename); v++)
			sectorBuffer[0x05 + v] = filename[v];

		for (; v < 16; v++)
			sectorBuffer[0x05 + v] = 160;

		sectorBuffer[0x15] = 0;
		sectorBuffer[0x16] = 0;
		sectorBuffer[0x17] = 0;

		for (int v = 0; v < 6; v++)
			sectorBuffer[0x18 + v] = 0;

		sectorBuffer[0x1e] = 0;
		sectorBuffer[0x1f] = 0;

		// write the data back
		writeSector();
		delete dirEntry;
		return ERR_OK;
	}

	delete dirEntry;
	return ERR_DISK_FULL;
}

int C1581::findDirectoryEntry(char *filename, char* extension, DirectoryEntry *dirEntry)
{
	static bool firstcall = true;
	static int dirctr = 0;
	static bool lastdirsector = false;
	int offset = 0;

	uint8_t filetypes[6][3] = {
		{ 'D', 'E', 'L' },
		{ 'S', 'E', 'Q' },
		{ 'P', 'R', 'G' },
		{ 'U', 'S', 'R' },
		{ 'R', 'E', 'L' },
		{ 'C', 'B', 'M' }
	};

	offset = getNextDirectoryEntry(&firstcall, &dirctr, &lastdirsector, dirEntry);

	while (offset != -1)
	{
		char tmpfilename[17];

		uint8_t z = 0;
		for(; z < 16; z++)
		{
			if (dirEntry->filename[z] == 0xa0)
				break;

			tmpfilename[z] = dirEntry->filename[z];
		}
		tmpfilename[z] = 0;

		if (strcmp(tmpfilename, filename) == 0)
		{
			firstcall = true;
			dirctr = 0;
			lastdirsector = false;

			return offset;
		}
		offset = getNextDirectoryEntry(&firstcall, &dirctr, &lastdirsector, dirEntry);
	}

	firstcall = true;
	dirctr = 0;
	lastdirsector = false;
	return -1;
}

int C1581::updateDirectoryEntry(char *filename, char* extension, DirectoryEntry *updateddirEntry)
{

	DirectoryEntry *dirEntry = new DirectoryEntry();
	int offset = findDirectoryEntry(filename, extension, dirEntry);

	if(offset == -1)
		return ERR_FILE_NOT_FOUND;

	sectorBuffer[offset + 0x02] = updateddirEntry->file_type;
	sectorBuffer[offset + 0x03] = updateddirEntry->first_data_track;
	sectorBuffer[offset + 0x04] = updateddirEntry->first_data_sector;
	sectorBuffer[offset + 0x1e] = updateddirEntry->size_lo;
	sectorBuffer[offset + 0x1f] = updateddirEntry->size_hi;

	for (int v = 0; v < 16; v++)
		sectorBuffer[offset + 0x05 + v]	= updateddirEntry->filename[v];

	delete [] dirEntry;
	write_d81();
	return ERR_OK;
}

int C1581::getBlocksFree(void)
{
	int blocksFree = 0;
	int bf2 = 0;
	int offset = 0x10;

	while(offset < 0xfa)
	{
		blocksFree += BAMCache1[offset];
		offset += 6;
	}

	offset = 0x10;
	while(offset < 256)
	{
		bf2 += BAMCache2[offset];
		blocksFree += BAMCache2[offset];
		offset += 6;
	}

	return blocksFree;
}

// File Functions
int C1581::readFile(uint8_t* filename, uint8_t* buffer, int *size)
{
	int resp = ERR_OK;
	static char lastfilename[16];
	*size = 0;

	if(!strcmp((const char*)filename,"*"))
	{
		static bool firstcall = true;
		static int dirctr = 0;
		static bool lastdirsector = false;
		int offset = 0;

		DirectoryEntry *dirEntry = new DirectoryEntry();

		offset = getNextDirectoryEntry(&firstcall, &dirctr, &lastdirsector, dirEntry);

		while (offset != -1)
		{
			if (dirEntry->file_type == 0x82 || dirEntry->file_type == 0xc2)
			{
				strcpy((char *)filename, (const char*)dirEntry->filename);

				for(int z=0; z<16;z++)
					if(filename[z] == 160)
						filename[z] = 0;

				break;
			}
			offset = getNextDirectoryEntry(&firstcall, &dirctr, &lastdirsector, dirEntry);
		}

		firstcall = true;
		dirctr = 0;
		lastdirsector = false;
		delete dirEntry;

		if (offset == -1)
			return ERR_FILE_NOT_FOUND;
	}

	while(1)
	{
		resp = getNextFileSector(filename);

		if(resp == IEC_LAST || resp == IEC_OK)
		{
			int tmp = BLOCK_SIZE;

			if(resp == IEC_LAST)
				tmp = sectorBuffer[1] + 1;

			for(int x=0; x<tmp; x++)
				buffer[(*size)++] = sectorBuffer[x];

			if(resp == IEC_LAST)
			{
				resp = ERR_OK;
				break;
			}

		}
		else
			break;
	}

	return resp;
}

int C1581::getNextFileSector(uint8_t* filename)
{
	static bool firstcall = true;
	uint8_t file_track;
	uint8_t file_sector;
	int ptr = 0;

	if (firstcall)
	{
		firstcall = false;
		int resp = getFileTrackSector((char *)filename, &file_track, &file_sector, false);

		if(resp != ERR_OK)
		{
			firstcall = true;
			return resp;
		}

		goTrackSector(file_track, file_sector);

	}
	else
	{
		goTrackSector(nxttrack, nxtsector);
	}
	
	nxttrack = sectorBuffer[0x00];
	nxtsector = sectorBuffer[0x01];
	
	if (nxttrack == 0x00)
	{
		firstcall = true;
		return IEC_LAST;
	}

	return IEC_OK;
}

int C1581::updateFileInfo(char *filename, uint16_t blocks)
{
	bool firstcall = true;
	bool readnextsector = false;
	int dirctr = 0;
	int offset = 0;
	bool lastdirsector = false;

	while(1)
	{
		if (firstcall == true)
		{
			firstcall = false;
			goTrackSector(40, 3);
			nxttrack = sectorBuffer[0x00];
			nxtsector = sectorBuffer[0x01];
			dirctr = 0;
		}
		else
		{
			if ((dirctr > 0) && (dirctr % 8 == 0))
			{
				if (lastdirsector)
					return -1;

				if(goTrackSector(nxttrack, nxtsector) == 0)
				{
					nxttrack = sectorBuffer[0x00];
					nxtsector = sectorBuffer[0x01];
					dirctr = 0;
				}
				else
				{
					return ERR_READ_ERROR;
				}
			}
		}

		offset = dirctr * 32;

		if (nxttrack == 0x00 && nxtsector == 0xff)
			lastdirsector = true;

		int flen = strlen(filename);
		int lctr = 0;
		for (int v = 0; v < flen; v++)
		{
			if (sectorBuffer[offset + 0x05 + v] == filename[v])
				lctr++;
		}

		if(lctr == flen)
		{
			// update the file type so that not a splat file
			sectorBuffer[offset + 0x02] = sectorBuffer[offset + 0x02] | 0x80;

			// update the size in blocks
			sectorBuffer[offset + 0x1e] = blocks & 0xff;
			sectorBuffer[offset + 0x1f] = (blocks >> 8) & 0xff;

			writeSector();
			return 0;
		}

		dirctr++;
	}
	return -1;
}

int C1581::get_last_error(char *buffer, int track, int sector)
{
	int len;
	for(int i = 0; i < NR_OF_EL(last_error_msgs); i++) {
		if(last_error == last_error_msgs[i].nr) {
			return sprintf(buffer,"%02d,%s,%02d,%02d\015", last_error,last_error_msgs[i].msg, track, sector);
		}
	}
    return sprintf(buffer,"99,UNKNOWN,00,00\015");
}
