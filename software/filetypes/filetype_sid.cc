#include <stdio.h>
#include "filetype_sid.h"
#include "filemanager.h"
#include "c64.h"
#include "c64_subsys.h"
#include "flash.h"
#include "menu.h"
#include "userinterface.h"
#include "stream_textlog.h"

extern uint8_t _sidcrt_65_start;
//extern uint8_t _sidcrt_65_end;

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

cart_def sid_cart = { 0x00, (void *)0, 0x1000, 0x01 | CART_RAM }; 

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
}

FileTypeSID :: ~FileTypeSID()
{
	if(file)
		fm -> fclose(file);
}

int FileTypeSID :: readHeader(void)
{
    int b, i, entries;
    uint32_t bytes_read;
    uint32_t *magic;
	FRESULT fres;
    
	// if we didn't open the file yet, open it now
	if(!file)
		fres = fm->fopen(node->getPath(), node->getName(), FA_READ, &file);
	else {		
		if(file->seek(0) != FR_OK) {
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
	if(fres != FR_OK) {
		fm->fclose(file);
		file = NULL;
		return -1;
	}
	
    // header checks
    magic = (uint32_t *)sid_header;
    if((*magic != magic_rsid)&&(*magic != magic_psid)) {
        printf("Filetype not as expected. (%08x)\n", *magic); 
		fm->fclose(file);
		file = NULL;
        return -1;
    }

    numberOfSongs = (int)sid_header[0x0f];
	fm->fclose(file);
	file = NULL;
	header_valid = true;
	return numberOfSongs;
}

void FileTypeSID :: showInfo()
{
	StreamTextLog stream(1024);

	char new_name[36];
    for(int b=0;b<3;b++) {
		int len = string_offsets[b+1]-string_offsets[b];
        memcpy(new_name, &sid_header[string_offsets[b]], len);
		new_name[len] = 0;
		stream.format("%s\n", new_name);
    }    
	stream.format("\nSID version: %b\n", sid_header[4]);
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
		readHeader();
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
		for(int i=0;i<numberOfSongs;i++) {
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
    if(strcmp(inf->extension, "SID")==0)
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

	if(selection == SIDFILE_PLAY_MAIN) {
		error = prepare(true);
	} else if(selection == SIDFILE_SHOW_INFO) {
		showInfo();
	} else if(selection <= 0x100) {
		song = uint16_t(selection);
		error = prepare(false);
	} else {
		this->cmd = 0;
		return -2;
	}
	if(error) {
		cmd->user_interface->popup((char*)errors[error], BUTTON_OK);
		if(file) {
			fm->fclose(file);
			file = NULL;
		}
		this->cmd = 0;
		return -3;
	}
	this->cmd = 0;
	return 0;
}

int FileTypeSID :: prepare(bool use_default)
{
	if(use_default) {
        printf("PREPARE DEFAULT SONG in %s.\n", node->getName());
    } else {
	   printf("PREPARE SONG #%d in %s.\n", song, node->getName());
	}
	
	FileInfo *info;
	uint32_t bytes_read;
	int  error = 0;
	int  length;
	
	// reload header, reset state of file
	uint32_t *magic = (uint32_t *)sid_header;
	if(!file) {
		if(fm->fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file) != FR_OK) {
			file = 0;
			return 1;
		}
	}
	if(file->seek(0) != FR_OK) {
		return 2;
	}
	if(file->read(sid_header, 0x7E, &bytes_read) != FR_OK) {
		return 3;
	}
    if((*magic != magic_rsid)&&(*magic != magic_psid)) {
        printf("Filetype not as expected. (%08x)\n", *magic); 
		return 4;
    }

	// extract default song
	if(use_default) {
		song = ((uint16_t)sid_header[0x10]) << 8;
	    song |= sid_header[0x11];
		printf("Default song = %d\n", song);
		if(!song)
			song = 1;
	}
	
	// write back the default song, for some players that only look here

	sid_header[0x10] = (song - 1) >> 8;
	sid_header[0x11] = (song - 1) & 0xFF;

	// get offset, file start, file end, update header.
	offset = uint32_t(sid_header[0x06]) << 8;
	offset |= sid_header[0x07];

	if(file->seek(offset) != FR_OK) {
		return 2;
	}
	start = uint16_t(sid_header[0x08]) << 8;
	start |= sid_header[0x09];

	if(start == 0) {
    	if(file->read(&start, 2, &bytes_read) != FR_OK) {
    		return 3;
    	}
    }
    offset += 2;
    start = le2cpu(start);

	length = (file->get_size() - offset) - 2;
	end = start + length;

	sid_header[0x7e] = uint8_t(end & 0xFF);
	sid_header[0x7f] = uint8_t(end >> 8);

	if (end < start) {
		printf("Wrap around $0000!\n");
		return 7;
	}

	// Now determine where to put the SID player header
    if (sid_header[4] < 0x02) {
        if(end <= 0xCF00) {
			player = 0xCF00;
        } else if (start >= 0x1000) {
            player = 0x0800;
        } else {
            player = 0x0400;
        }
    } else {
        // version 2 and higher
        if(sid_header[0x78] == 0xFF) {
            printf("No reloc space.\n");
			return 6;
		}
        if(sid_header[0x78] == 0x00) {
            printf("Clean SID file.. checking start/stop\n");
            if(end <= 0xCF00) {
				player = 0xCF00;
            } else if (start >= 0x1000) {
                player = 0x0800;
            } else {
                player = 0x0400;
            }
        } else {
            if(sid_header[0x79] < 1) {
                printf("Space for driver too small.\n");
				return 6;
			}
            player = ((uint16_t)sid_header[0x78]) << 8;
        }            
    }
    printf("Player address: %04x.\n", player);

	// convert big endian to little endian format
	uint16_t *pus = (uint16_t *)&sid_header[4];
	for(int i=0;i<7;i++,pus++)
		*pus = swap(*pus);
	header_valid = false;

	// Now, start to access the C64..
	SubsysCommand *c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_STOP_COMMAND, (int)0, "", "");
	c64_command->execute();

	memset((void *)C64_MEMORY_BASE, 0, 1024);
	C64_POKE(0x0165, player >> 8); // Important: Store player header loc!
	// make memory testing obsolete
	C64_POKE(0x0284, 0xA0);
	C64_POKE(0x0282, 0x08);
	C64_POKE(0x0288, 0x04);
    C64_POKE(0xDD0D, 0x7F);
    C64_PEEK(0xDD0D);
	C64_POKE(2, 0xAA); // indicate to the cart that we want to invoke the sid player

	C64_POKE(0x0162, 0);
	C64_POKE(0x0164, player);
	C64_POKE(0x0165, player >> 8);

	// leave the browser, and smoothly transition to
	// sid cart.
    sid_cart.custom_addr = (void *)&_sidcrt_65_start;
	c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_START_CART, (int)&sid_cart, "", "");
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
	uint32_t bytes_read;
	
	printf("Loading SID..\n");
	C64 *commie = (C64 *)c64;

	// handshake with sid player cart
	commie->stop(false);

	C64_POKE(0x0162, 0);
	C64_POKE(0x0164, player);
	C64_POKE(0x0165, player >> 8);

	int timeout = 0;
	while(C64_PEEK(2) != 0x01) {
		commie->resume();
		timeout++;
		if(timeout == 30) {
            printf("Time out!\n");
			fm->fclose(file);
			file = NULL;
			commie->init_cartridge();
			return;
		}
		wait_ms(25);
		printf("/");
		commie->stop(false);
	}
	
    // clear the entire memory
	memset((void *)(C64_MEMORY_BASE + 0x0400), 0x00, 0xFC00);

	// copy sid header into C64 memory
	memcpy((void *)(C64_MEMORY_BASE + player), sid_header, 0x80);

	// load file in C64 memory
    /* Now actually load the file */
    int total_trans = 0;
    int max_length = 65536;

    int block = (8192 - offset) & 0x1FF;

	uint8_t dma_load_buffer[512];
	uint8_t *dest = (uint8_t *)(C64_MEMORY_BASE);
	int offset = int(start);
	while (max_length > 0) {
		file->read(dma_load_buffer, block, &bytes_read);
    	total_trans += bytes_read;
    	if ((bytes_read + offset) > 65535) {
    		//printf("\nRead: %d\nmemcpy(%p, %p, %d);\n", bytes_read, &dest[offset], dma_load_buffer, 65536 - offset);
    		memcpy(&dest[offset], dma_load_buffer, 65536 - offset);
    		//printf("memcpy(%p, %p, %d);\n", dest, &dma_load_buffer[65536-offset], bytes_read - (65536 - offset));
    		memcpy(dest, &dma_load_buffer[65536-offset], bytes_read - (65536 - offset));
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

	printf("Bytes loaded: %d. $%4x-$%4x\n", total_trans, start, end);

	C64_POKE(0x002D, end);
	C64_POKE(0x002E, end >> 8);
	C64_POKE(0x002F, end);
	C64_POKE(0x0030, end >> 8);
	C64_POKE(0x0031, end);
	C64_POKE(0x0032, end >> 8);
	C64_POKE(0x00AE, end);
	C64_POKE(0x00AF, end >> 8);
	//C64_POKE(0x0002, song);

	C64_POKE(0x0162, 0xAA); // Handshake
	C64_POKE(0x00BA, 8);    // Load drive
	C64_POKE(0x0090, 0x40); // Load status
	C64_POKE(0x0035, 0);    // FRESPC
	C64_POKE(0x0036, 0xA0);
	
//    SID_REGS(0) = 0x33; // start trace
    
	commie->resume();

	fm->fclose(file);
	file = NULL;

    c64_subsys->restoreCart();
	
	return;
}
