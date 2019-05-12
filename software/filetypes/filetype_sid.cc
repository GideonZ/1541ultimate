#include <stdio.h>
#include "filetype_sid.h"
#include "filemanager.h"
#include "c64.h"
#include "c64_subsys.h"
#include "flash.h"
#include "menu.h"
#include "userinterface.h"
#include "stream_textlog.h"
#include "init_function.h"
#include "dump_hex.h"
#include "sid_config.h"

extern uint8_t _sidcrt_bin_start;
extern uint8_t _sidcrt_bin_end;
extern uint8_t _muscrt_bin_start;
extern uint8_t _muscrt_bin_end;
extern uint8_t _musplayer_bin_start;
extern uint8_t _musplayer_bin_end;
extern uint8_t _basic_bin_start;

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_sid(FileType :: getFileTypeFactory(), FileTypeSID :: test_type);

#define SIDFILE_PLAY_MAIN 0x5301
#define SIDFILE_PLAY_TUNE 0x5302
// all codes starting from 0x0000 until 0xFF are execution codes for a specific subtune
#define SIDFILE_PLAY_LAST 0x5402
#define SIDFILE_SHOW_INFO 0x5403

#if NIOS
const uint32_t magic_psid = 0x44495350; // little endian
const uint32_t magic_rsid = 0x44495352; // little endian
#else
const uint32_t magic_psid = 0x50534944; // big endian
const uint32_t magic_rsid = 0x52534944; // big endian
#endif

const int string_offsets[4] = { 0x16, 0x36, 0x56, 0x76 };

cart_def sid_cart = { ID_SIDCART, (void *)0, 0x4000, CART_TYPE_16K | CART_RAM };
cart_def mus_cart = { ID_SIDCART, (void *)0, 0x4000, CART_TYPE_16K | CART_RAM };

static inline uint16_t swap_word(uint16_t p)
{
#if NIOS
	return p;
#else
	uint16_t out = (p >> 8) | (p << 8);
	return out;
#endif
}

#define le2cpu  swap_word
#define cpu2le  swap_word
#define swap(p) (((p) >> 8) | ((p) << 8))

static void initSidCart(void *object, void *param)
{
    int size = (int)&_sidcrt_bin_end - (int)&_sidcrt_bin_start;
    uint8_t *sid_rom_area = new uint8_t[16384];
    sid_cart.custom_addr = sid_rom_area;
    //sid_cart.length = size;
    memcpy(sid_rom_area, &_sidcrt_bin_start, size);
    printf("%d bytes copied into sid_cart.\n", size);
    memcpy(sid_rom_area + 0x2000, &_basic_bin_start, 8192);

    int mus_crt_size = (int)&_muscrt_bin_end - (int)&_muscrt_bin_start;
    uint8_t *mus_rom_area = new uint8_t[16384];
    mus_cart.custom_addr = mus_rom_area;
    memcpy(mus_rom_area, &_muscrt_bin_start, mus_crt_size);
    printf("%d bytes copied into mus_cart.\n", mus_crt_size);
    memcpy(mus_rom_area + 0x2000, &_basic_bin_start, 8192);
}
InitFunction sidCart_initializer(initSidCart, NULL, NULL);


// on U64, this function will fall through in the audio configurator. On other platforms, the function below (empty) will be called.
bool SidAutoConfig(int count, t_sid_definition *requests) __attribute__((weak));

bool SidAutoConfig(int count, t_sid_definition *requests)
{

}

bool FileTypeSID :: ConfigSIDs(void)
{
    t_sid_definition requests[3]; // SID tunes never ask for more than 3 sids
    int count = 1;
    requests[0].baseAddress = 0x40; // D400
    requests[0].sidType = 3; // not specified

	int version = ((int)sid_header[0x04]) << 8;
    version |= (int)sid_header[0x05];

    if (version >= 2) {
        if ((flags >> 4) & 3) { // if the SID file specifies the type, we'll copy it
            requests[0].sidType = (flags >> 4) & 3;
        }
        if ((version >= 3) && (sid_header[0x7A])) {
            requests[1].baseAddress = sid_header[0x7A];
			requests[1].sidType = 3; // not specified
            count = 2;

			if ((flags >> 6) & 3) { // if the SID file specifies the type, we'll copy it
				requests[1].sidType = (flags >> 6) & 3;
			}
        }
        if ((version >= 4) && (sid_header[0x7B])) {
            requests[2].baseAddress = sid_header[0x7B];
		    requests[2].sidType = 3; // not specified
            count = 3;

			if ((flags >> 8) & 3) { // if the SID file specifies the type, we'll copy it
				requests[2].sidType = (flags >> 8) & 3;
			}
        }
    }
    return SidAutoConfig(count, requests);
}

/*************************************************************/
/* SID File Browser Handling                                 */
/*************************************************************/
FileTypeSID :: FileTypeSID(BrowsableDirEntry *node)
{
    fm = FileManager :: getFileManager();
	this->node = node;
	printf("Creating SID type from info: %s\n", node->getName());
    file = NULL;
    header_valid = false;
    numberOfSongs = 0;
    cmd = NULL;

	FileInfo *inf = node->getInfo();
	mus_file = strcmp(inf->extension, "MUS") == 0 || strcmp(inf->extension, "STR") == 0;
}

FileTypeSID :: ~FileTypeSID()
{
	if (file)
		fm -> fclose(file);
}

int FileTypeSID :: readHeader(void)
{
    int b, i, entries;
    uint32_t bytes_read;
    uint32_t *magic;
	FRESULT fres;

	// if we didn't open the file yet, open it now
	if (!file)
		fres = fm->fopen(node->getPath(), node->getName(), FA_READ, &file);
	else {
		if (file->seek(0) != FR_OK) {
			fm->fclose(file);
			file = NULL;
			return -1;
		}
	}
	if (!file) {
		printf("opening file was not successful. %s\n", FileSystem :: get_error_string(fres));
		return -3;
	}
    fres = file->read(sid_header, 0x7e, &bytes_read);
	if (fres != FR_OK) {
		fm->fclose(file);
		file = NULL;
		return -1;
	}

    // header checks
    magic = (uint32_t *)sid_header;
    if ((*magic != magic_rsid)&&(*magic != magic_psid)) {
        printf("Filetype not as expected. (%08x)\n", *magic);
		fm->fclose(file);
		file = NULL;
        return -1;
    }

	numberOfSongs = ((int)sid_header[0x0e]) << 8;
    numberOfSongs |= (int)sid_header[0x0f];

	fm->fclose(file);
	file = NULL;
	header_valid = true;
	return numberOfSongs;
}

int FileTypeSID :: createMusHeader(void)
{
    for (int b = 0; b < 0x80; b++) {
		sid_header[b] = 0;
    }

	sid_header[0x00] = 'P';
	sid_header[0x01] = 'S';
	sid_header[0x02] = 'I';
	sid_header[0x03] = 'D';

	sid_header[0x05] = 0x03;	// version of header, 3 for support of 2nd SID chip
	sid_header[0x07] = 0x7C;	// header size

	sid_header[0x0A] = 0xEC;	// init address
	sid_header[0x0B] = 0x60;
	sid_header[0x0C] = 0xEC;	// play address
	sid_header[0x0D] = 0x80;

	sid_header[0x0F] = 0x01;	// number of songs 1
	sid_header[0x11] = 0x01;	// default song 1

	sid_header[0x15] = 0x01;	// play at CIA speed

	sid_header[0x36] = 0x3C;	// default author unknown
	sid_header[0x37] = 0x3F;
	sid_header[0x38] = 0x3E;

	sid_header[0x56] = 0x31;	// default release year unknown
	sid_header[0x57] = 0x39;
	sid_header[0x58] = 0x3F;
	sid_header[0x59] = 0x3F;

	sid_header[0x5A] = 0x20;

	sid_header[0x5B] = 0x3C;	// default publisher unknown
	sid_header[0x5C] = 0x3F;
	sid_header[0x5D] = 0x3E;

	sid_header[0x77] = 0x29;	// default flags set for 8580, NTSC and MUS data only

	// set filename as title
	const char *filename = node->getName();
	int size = strlen(filename);

	// truncate filename where extension begins
	for (int i = size - 1; i > 0; i--) {
		if (filename[i] == '.') {
			if (i > 0x20) {
				size = 0x20;
			} else {
				size = i;
			}
			break;
		}
	}

    for (int b = 0; b < size; b++) {
		char c = filename[b];
		if (c == 0) {
			break;
		}
		if (c == 0x5f) {
			c = 0x20;			// convert underscore to space
		}
		sid_header[0x16 + b] = c;
    }

	header_valid = true;
	numberOfSongs = 1;

	return numberOfSongs;
}

void FileTypeSID :: showInfo()
{
	StreamTextLog stream(1024);

	char new_name[36];
    for (int b = 0; b < 3; b++) {
		int len = string_offsets[b+1]-string_offsets[b];
        memcpy(new_name, &sid_header[string_offsets[b]], len);
		new_name[len] = 0;
		stream.format("%s\n", new_name);
		if (mus_file) {
			break;		// only show the title of the MUS file
		}
    }

	if (!mus_file) {
		int version = ((int)sid_header[0x04]) << 8;
		version |= (int)sid_header[0x05];

		stream.format("\nSID version: %d\n", version);
	}

    stream.format("\nNumber of songs: %d\n", numberOfSongs);
	uint16_t sng = ((uint16_t)sid_header[0x10]) << 8;
    sng |= sid_header[0x11];
    stream.format("Default song = %d\n", sng);

	cmd->user_interface->run_editor(stream.getText());
	// stream gets out of scope.
}

int FileTypeSID :: fetch_context_items(IndexedList<Action *> &list)
{
	if (!header_valid) {
		if (mus_file) {
			createMusHeader();
		} else {
			readHeader();
		}
	}
	if (!header_valid) {
		return 0;
	}
	if (!c64->exists()) {
		return 0;
	}
	list.append(new Action("Play Main Tune", FileTypeSID :: execute_st, SIDFILE_PLAY_MAIN, (int)this ));
	list.append(new Action("Show Info", FileTypeSID :: execute_st, SIDFILE_SHOW_INFO, (int)this ));

    if (numberOfSongs > 1) {
		char buffer[16];
		for (int i = 0; i < numberOfSongs; i++) {
			sprintf(buffer, "Play Song %d", i+1);
			list.append(new Action(buffer, FileTypeSID :: execute_st, i+1, (int)this ));
		}
		return numberOfSongs + 2;
    }
    return 2;
}

FileType *FileTypeSID :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if ((strcmp(inf->extension, "SID")==0) || (strcmp(inf->extension, "MUS")==0) || (strcmp(inf->extension, "STR")==0))
        return new FileTypeSID(obj);
    return NULL;
}

int FileTypeSID :: execute_st(SubsysCommand *cmd) {
	printf("FileTypeSID :: execute_st: Mode = %p\n", cmd->mode);
	return ((FileTypeSID *)cmd->mode)->execute(cmd);
}

int FileTypeSID :: execute(SubsysCommand *cmd)
{
	int error = 0;
	const char *errors[] = { "", "Can't open file",
		"File seek failed", "Can't read header",
		"File type error", "Internal error",
		"No memory location for player data",
	    "Illegal: Load rolls over $FFFF" };

	int selection = cmd->functionID;
	this->cmd = cmd;

	if (selection == SIDFILE_PLAY_MAIN) {
		error = prepare(true);
	} else if (selection == SIDFILE_SHOW_INFO) {
		showInfo();
	} else if (selection <= 0x100) {
		song = uint16_t(selection);
		error = prepare(false);
	} else {
		this->cmd = 0;
		return -2;
	}
	if (error) {
		cmd->user_interface->popup((char*)errors[error], BUTTON_OK);
		if (file) {
			fm->fclose(file);
			file = NULL;
		}
		this->cmd = 0;
		return -3;
	}
	this->cmd = 0;
	return 0;
}

void FileTypeSID :: readSongLengths(void)
{
	if (!mus_file) {
		Path *slPath = fm->get_new_path("sid file");
		File *sslFile;

		slPath->cd(cmd->path.c_str());
		slPath->cd("SONGLENGTHS");
		char filename[80];
		strncpy(filename, cmd->filename.c_str(), 79);
		filename[79] = 0;

		set_extension(filename, ".ssl", 80);

		uint8_t *sid_rom = (uint8_t *)sid_cart.custom_addr;
		// just clear the first few bytes for the player to know there
		// are no song lengths loaded
		memset(sid_rom + 0x3000, 0, 512);

		uint32_t songLengthsArrayLength = 0;
		if (fm->fopen(slPath->get_path(), filename, FA_READ, &sslFile) == FR_OK) {
			sslFile->read(sid_rom + 0x3000, 512, &songLengthsArrayLength);
			printf("Song length array loaded. %d bytes\n", songLengthsArrayLength);
			fm->fclose(sslFile);
		} else {
			printf("Cannot open file with song lengths.\n");
		}

		fm->release_path(slPath);
	} else {
		uint8_t *sid_rom = (uint8_t *)mus_cart.custom_addr;
		memset(sid_rom + 0x3000, 0, 512);
		sid_rom[0x3000] = 3;			// default is 3 minutes for MUS songs
	}
}

bool FileTypeSID :: loadStereoMus(int offset)
{
	if (mus_file) {
		File *strFile;

		char filename[80];
		strncpy(filename, cmd->filename.c_str(), 79);
		filename[79] = 0;

		set_extension(filename, ".str", 80);

		if (fm->fopen(node->getPath(), filename, FA_READ, &strFile) == FR_OK) {
			if (strFile->seek(2) != FR_OK) {	// skip first 2 bytes
				return false;
			}

			int offsetEnd = loadFile(strFile, offset);
			if (offsetEnd > offset) {
				end = offsetEnd;
				return true;
			}
		}
	}
	return false;
}

int FileTypeSID :: prepare(bool use_default)
{
	if (use_default) {
		printf("PREPARE DEFAULT SONG in %s.\n", node->getName());
    } else {
		printf("PREPARE SONG #%d in %s.\n", song, node->getName());
	}

	FileInfo *info;
	uint32_t bytes_read;
	int  error = 0;
	int  length;

	char filename[80];
	strncpy(filename, cmd->filename.c_str(), 79);
	filename[79] = 0;

	if (mus_file) {
		// when .str file is selected, first load the .mus file
		set_extension(filename, ".mus", 80);
	}

	// reload header, reset state of file
	uint32_t *magic = (uint32_t *)sid_header;
	if (!file) {
		if (fm->fopen(cmd->path.c_str(), filename, FA_READ, &file) != FR_OK) {
			file = 0;
			return 1;
		}
	}
	if (file->seek(0) != FR_OK) {
		return 2;
	}

	if (mus_file) {
		createMusHeader();

		start = 0x1000;			// sidplayer music data at $1000
		offset = 2;

		if (file->seek(offset) != FR_OK) {
			return 2;
		}
	} else {
		if (file->read(sid_header, 0x7E, &bytes_read) != FR_OK) {
			return 3;
		}
		if ((*magic != magic_rsid)&&(*magic != magic_psid)) {
			printf("Filetype not as expected. (%08x)\n", *magic);
			return 4;
		}

		// get offset, file start, file end, update header.
		offset = uint32_t(sid_header[0x06]) << 8;
		offset |= sid_header[0x07];

		if (sid_header[0x77] & 1 == 1) {
			createMusHeader();
			mus_file = true;

			song = 1;
			start = 0x1000;		// sidplayer music data at $1000

			offset += 2;
			if (file->seek(offset) != FR_OK) {
				return 2;
			}
		} else {
			if (file->seek(offset) != FR_OK) {
				return 2;
			}
			start = uint16_t(sid_header[0x08]) << 8;
			start |= sid_header[0x09];

			if (start == 0) {
				if (file->read(&start, 2, &bytes_read) != FR_OK) {
					return 3;
				}
			}
			offset += 2;
			start = le2cpu(start);
		}
	}

	length = (file->get_size() - offset);
	end = start + length;

	sid_header[0x7e] = uint8_t(end & 0xFF);
	sid_header[0x7f] = uint8_t(end >> 8);

	if (end < start) {
		printf("Wrap around $0000!\n");
		return 7;
	}

	if (start >= 0x03c0) {
		header_location = 0x0340;
	} else if (end < 0xff70) {
		header_location = 0xff70;
	} else {
		printf("Space for header too small.\n");
		return 6;
	}

    printf("SID header address: %04x.\n", header_location);

	// extract default song
	if (use_default) {
		song = ((uint16_t)sid_header[0x10]) << 8;
	    song |= sid_header[0x11];
		printf("Default song = %d\n", song);
		if (!song)
			song = 1;
	}

	// write back the default song, for some players that only look here
	sid_header[0x10] = (song - 1) >> 8;
	sid_header[0x11] = (song - 1) & 0xFF;

	flags = ((uint16_t)sid_header[0x76]) << 8;
	flags |= sid_header[0x77];

	// convert big endian to little endian format
	uint16_t *pus = (uint16_t *)&sid_header[4];
	for (int i = 0; i < 7; i++, pus++)
		*pus = swap(*pus);
	header_valid = false;

	readSongLengths();

	// Now, start to access the C64..
	SubsysCommand *c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_STOP_COMMAND, (int)0, "", "");
	c64_command->execute();

	memset((void *)C64_MEMORY_BASE, 0, 1024);
	// make memory testing obsolete
	C64_POKE(0x0282, 0x08);
	C64_POKE(0x0284, 0xA0);
	C64_POKE(0x0288, 0x04);
    C64_POKE(0xDD0D, 0x7F);
    C64_PEEK(0xDD0D);
	C64_POKE(2, 0xAA);		// indicate to the cart that we want to invoke the sid player

	C64_POKE(0x0162, 0);	// clear address for handshake DMA load of SID file

	// leave the browser, and smoothly transition to sid/mus cart.
	if (mus_file) {
		c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_START_CART, (int)&mus_cart, "", "");
	} else {
		c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_START_CART, (int)&sid_cart, "", "");
	}
	c64_command->execute();

	load();
	return 0;
}

/******************************************************
 * Invoking the SID player
 ******************************************************/
// precondition: C64 is booted with sid cart
// we are not in the browser
// file is open, and positioned at data position
void FileTypeSID :: load(void)
{
	printf("Loading SID..\n");

	// handshake with sid player cart
	c64->stop(false);

	int timeout = 0;
	while (C64_PEEK(2) != 0x01) {
		c64->resume();
		timeout++;
		if (timeout == 30) {
            printf("Time out!\n");
			fm->fclose(file);
			file = NULL;
			c64->init_cartridge();
			return;
		}
		wait_ms(25);
		printf("/");
		c64->stop(false);
	}

    // clear the entire memory
	memset((void *)(C64_MEMORY_BASE + 0x0340), 0x00, 0xFCC0);

	int offsetLoadEnd = loadFile(file, start);

	if (mus_file) {
		bool stereo = loadStereoMus(offsetLoadEnd);

		// install mus player
		int mus_player_size = (int)&_musplayer_bin_end - (int)&_musplayer_bin_start;
		uint8_t *dest = (uint8_t *)(C64_MEMORY_BASE);
		memcpy(&dest[0xe000], &_musplayer_bin_start, mus_player_size);

		C64_POKE(0xEC6E, start);
		C64_POKE(0xEC70, start >> 8);	// sidplayer data location

		if (stereo) {
			sid_header[0x0A] = 0x90;
			sid_header[0x0B] = 0xFC;	// stereo init address (in little endian)
			sid_header[0x0C] = 0x96;
			sid_header[0x0D] = 0xFC;	// stereo play address (in little endian)

			sid_header[0x7A] = 0x50;	// address of second SID chip is at $D500
			sid_header[0x77] |= 0x80;	// set second SID to 8580

			C64_POKE(0xFC6E, offsetLoadEnd);
			C64_POKE(0xFC70, offsetLoadEnd >> 8);	// sidplayer data location start of stereo file
		}

		if (end >= 0xC400) {
			sid_header[0x78] = 0x04;
			sid_header[0x79] = 0x0C;
		}
	}

	sid_header[0x7e] = uint8_t(end & 0xFF);
	sid_header[0x7f] = uint8_t(end >> 8);

	ConfigSIDs();

	// copy sid header into C64 memory
	memcpy((void *)(C64_MEMORY_BASE + header_location), sid_header, 0x80);

	C64_POKE(0x0164, header_location);
	C64_POKE(0x0165, header_location >> 8);

	C64_POKE(0x002D, end);
	C64_POKE(0x002E, end >> 8);
	C64_POKE(0x002F, end);
	C64_POKE(0x0030, end >> 8);
	C64_POKE(0x0031, end);
	C64_POKE(0x0032, end >> 8);
	C64_POKE(0x00AE, end);
	C64_POKE(0x00AF, end >> 8);

	C64_POKE(0x0162, 0xAA); // Handshake
	C64_POKE(0x00BA, 8);    // Load drive
	C64_POKE(0x0090, 0x40); // Load status
	C64_POKE(0x0035, 0);    // FRESPC
	C64_POKE(0x0036, 0xA0);

//    SID_REGS(0) = 0x33; // start trace

	c64->resume();

	file = NULL;

	// In this special case, we do NOT restore the cartridge right away,
	// but we do this as soon as the button is pressed, not earlier.
	// c64_subsys->restoreCart();

	return;
}

int FileTypeSID :: loadFile(File *file, int offset)
{
	// load file in C64 memory
    int total_trans = 0;
    int max_length = 65536;
	uint32_t bytes_read;

    int block = (8192 - offset) & 0x1FF;

	uint8_t dma_load_buffer[512];
	uint8_t *dest = (uint8_t *)(C64_MEMORY_BASE);
	while (max_length > 0) {
		file->read(dma_load_buffer, block, &bytes_read);
    	total_trans += bytes_read;
    	if ((bytes_read + offset) > 65535) {
    		//printf("\nRead: %d\nmemcpy(%p, %p, %d);\n", bytes_read, &dest[offset], dma_load_buffer, 65536 - offset);
    		memcpy(&dest[offset], dma_load_buffer, 65536 - offset);
    		//printf("memcpy(%p, %p, %d);\n", dest, &dma_load_buffer[65536-offset], bytes_read - (65536 - offset));
    		memcpy(dest, &dma_load_buffer[65536 - offset], bytes_read - (65536 - offset));
    	} else {
    		memcpy(&dest[offset], dma_load_buffer, bytes_read);
    	}
    	offset += bytes_read;
    	offset &= 0xFFFF;
    	if (bytes_read < block) {
    		break;
    	}
    	max_length -= bytes_read;
    	block = 512;
    }
	fm->fclose(file);

	printf("Bytes loaded: %d. $%4x-$%4x\n", total_trans, start, end);

	return offset;
}
