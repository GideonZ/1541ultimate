#include "filetype_sid.h"
#include "filemanager.h"
#include "c64.h"
#include "flash.h"
#include "menu.h"
#include "userinterface.h"

extern BYTE _binary_sidcrt_65_start;
extern BYTE _binary_sidcrt_65_end;

// tester instance
FileTypeSID tester_sid(file_type_factory);

#define SIDFILE_PLAY_MAIN 0x5301
#define SIDFILE_PLAY_TUNE 0x5302
// all codes starting from 0x5303 until 5401 are execution codes for a specific subtune 
#define SIDFILE_PLAY_LAST 0x5402
#define SIDFILE_SHOW_SUB  0x5403
#define SIDFILE_LOADNRUN  0x54FF

const DWORD magic_psid = 0x50534944; // big endian assumed
const DWORD magic_rsid = 0x52534944; // big endian assumed
const int string_offsets[4] = { 0x16, 0x36, 0x56, 0x76 };

cart_def sid_cart = { 0x00, (void *)0, 0x1000, 0x01 | CART_RAM }; 

static inline WORD swap_word(WORD p)
{
	WORD out = (p >> 8) | (p << 8);
	return out;
}
#define le2cpu swap_word
#define cpu2le swap_word
#define swap   swap_word

/*************************************************************/
/* SID File Browser Handling                                 */
/*************************************************************/

FileTypeSID :: FileTypeSID(FileTypeFactory &fac) : FileDirEntry(NULL, (FileInfo *)NULL)
{
    fac.register_type(this);
    file = NULL;
}

FileTypeSID :: FileTypeSID(PathObject *par, FileInfo *fi) : FileDirEntry(par, fi)
{
    printf("Creating SID type from info: %s\n", fi->lfname);
    file = NULL;
}

FileTypeSID :: ~FileTypeSID()
{
	if(file)
		root.fclose(file);
}

int FileTypeSID :: fetch_children(void)
{
    int b, i, entries;
    UINT bytes_read;
    DWORD *magic;
	FRESULT fres;
    
	// if we fetched them, we don't need to do it again.
	entries = children.get_elements();
	if(entries)
		return entries;

	// if we didn't open the file yet, open it now
	if(!file)
		file = root.fopen(this, FA_READ);
	else {		
		if(file->seek(0) != FR_OK) {
			root.fclose(file);
			file = NULL;
			return -1;
		}
	}
		
    fres = file->read(sid_header, 0x7e, &bytes_read);
	if(fres != FR_OK) {
		root.fclose(file);
		file = NULL;
		return -1;
	}
	
    // header checks
    magic = (DWORD *)sid_header;
    if((*magic != magic_rsid)&&(*magic != magic_psid)) {
        printf("Filetype not as expected. (%08x)\n", *magic); 
		root.fclose(file);
		file = NULL;
        return -1;
    }

	char new_name[36];
	
    for(b=0;b<3;b++) {
		int len = string_offsets[b+1]-string_offsets[b];
        memcpy(new_name, &sid_header[string_offsets[b]], len);
		new_name[len] = 0;
        for(i=0;i<len;i++) {
            new_name[i] |= 0x80;
        }
		children.append(new PathObject(this, new_name));
    }    

    // number of tunes is in 0x0F (assuming never more than 256 tunes)
    for(b=1;b<=sid_header[0x0f];b++) {
		children.append(new SidTune(this, b));
    }
	root.fclose(file);
	file = NULL;

	return children.get_elements();
}

int FileTypeSID :: fetch_context_items(IndexedList<PathObject *> &list)
{
    list.append(new MenuItem(this, "Play Main Tune", SIDFILE_PLAY_MAIN ));
    list.append(new MenuItem(this, "Select Sub Tune", SIDFILE_SHOW_SUB ));
    return 2 + FileDirEntry :: fetch_context_items_actual(list);
}

FileDirEntry *FileTypeSID :: test_type(PathObject *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "SID")==0)
        return new FileTypeSID(obj->parent, inf);
    return NULL;
}

void FileTypeSID :: execute(int selection)
{
	int error = 0;
	const char *errors[] = { "", "Can't open file",
		"File seek failed", "Can't read header",
		"File type error", "Internal error",
		"No memory location for player data" };
		
    if(selection == SIDFILE_SHOW_SUB) {
        push_event(e_browse_into);
    } else if(selection == SIDFILE_PLAY_MAIN) {
		error = prepare(true);
	} else if((selection >= SIDFILE_PLAY_TUNE) && (selection <= SIDFILE_PLAY_LAST)) {
		song = WORD(selection) - SIDFILE_PLAY_TUNE;
		error = prepare(false);
	} else if(selection == SIDFILE_LOADNRUN) {
		load();
	} else {
		FileDirEntry :: execute(selection);
		return;
	}
	if(error)
		user_interface->popup((char*)errors[error], BUTTON_OK);
}

int FileTypeSID :: prepare(bool use_default)
{
    if(use_default) {
        printf("PREPARE DEFAULT SONG in %s.\n", get_name());
    } else {
	   printf("PREPARE SONG #%d in %s.\n", song, get_name());
	}
	
	FileInfo *info;
	UINT bytes_read;
	WORD *pus;
	WORD offset;
	int  error = 0;
	int  length;
	
	// reload header, reset state of file
	DWORD *magic = (DWORD *)sid_header;
	if(!file)
		file = root.fopen(this, FA_READ);
	if(!file) {
		error = 1;
		goto handle_error;
	}
	if(file->seek(0) != FR_OK) {
		error = 2;
		goto handle_error;
	}
	if(file->read(sid_header, 0x7E, &bytes_read) != FR_OK) {
		error = 3;
		goto handle_error;
	}
    if((*magic != magic_rsid)&&(*magic != magic_psid)) {
        printf("Filetype not as expected. (%08x)\n", *magic); 
		error = 4;
		goto handle_error;
    }

	// extract default song
	if(use_default) {
		pus = (WORD *)&sid_header[0x10];
		song = *pus;
		printf("Default song = %d\n", song);
		if(!song)
			song = 1;
	}
	
	// write back the default song, for some players that only look here
	pus = (WORD *)&sid_header[0x10];
	*pus = song-1;

	// get offset, file start, file end, update header.
	pus = (WORD *)&sid_header[0x06];
	offset = *pus;
	if(file->seek(offset) != FR_OK) {
		error = 2;
		goto handle_error;
	}
    pus = (WORD *)&sid_header[0x08];
    start = *pus;
    if(start == 0) {
    	if(file->read(&start, 2, &bytes_read) != FR_OK) {
    		error = 3;
    		goto handle_error;
    	}
    }
    start = le2cpu(start);

	info = file->node->get_file_info();
	if(!info) {
		error = 5;
		goto handle_error;
	}
	length = (info->size - offset) - 2;
	end = start + length;
	pus = (WORD *)&sid_header[0x7e];
	*pus = cpu2le(end);

	// Now determine where to put the SID player header
    if (sid_header[4] < 0x02) {
        if (start >= 0x1000) {
            player = 0x0800;
        } else if(end <= 0xCF00) {
            player = 0xCF00;
        } else {
            player = 0x0400;
        }
    } else {
        // version 2 and higher
        if(sid_header[0x78] == 0xFF) {
            printf("No reloc space.\n");
			error = 6;
			goto handle_error;
		}
        if(sid_header[0x78] == 0x00) {
            printf("Clean SID file.. checking start/stop\n");
            if (start >= 0x1000) {
                player = 0x0800;
            } else if(end <= 0xCF00) {
                player = 0xCF00;
            } else {
                player = 0x0400;
            }
        } else {
            if(sid_header[0x79] < 1) {
                printf("Space for driver too small.\n");
				error = 6;
				goto handle_error;
			}
            player = ((WORD)sid_header[0x78]) << 8;
        }            
    }
    printf("Player address: %04x.\n", player);

	// convert big endian to little endian format
	pus = (WORD *)&sid_header[4];
	for(int i=0;i<7;i++,pus++)
		*pus = swap(*pus);

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
    sid_cart.custom_addr = (void *)&_binary_sidcrt_65_start;
	push_event(e_unfreeze, (void *)&sid_cart, 1);

	// call us back after the cartridge has been started
	push_event(e_path_object_exec_cmd, this, SIDFILE_LOADNRUN); 

	return 0;

handle_error:
    printf("Error = %d... ", error);
	if(file)
		root.fclose(file);
	return error;
}

/******************************************************
 * Operations on a single tune..
 ******************************************************/
 
int SidTune :: fetch_context_items(IndexedList<PathObject *> &list)
{
    list.append(new MenuItem(parent, "Play Tune", SIDFILE_PLAY_TUNE + index ));
    return 1;
}
// That was simple, eh?

char *SidTune :: get_display_string()
{
	static char buf[16];
	sprintf(buf, "Tune #%d", index);
	return buf;
}
/******************************************************
 * Invoking the SID player
 ******************************************************/
// precondition: C64 is booted with sid cart
// we are not in the browser
// file is open, and positioned at data position
void FileTypeSID :: load(void)
{
	UINT bytes_read;
	
	// handshake with sid player cart
	c64->stop(false);

	C64_POKE(0x0162, 0);
	C64_POKE(0x0164, player);
	C64_POKE(0x0165, player >> 8);

	int timeout = 0;
	while(C64_PEEK(2) != 0x01) {
		c64->resume();
		timeout++;
		if(timeout == 30) {
            printf("Time out!\n");
			root.fclose(file);
			file = NULL;
			c64->init_cartridge();
			return;
		}
		wait_ms(25);
		printf("/");
		c64->stop(false);
	}
	
    // clear the entire memory
	memset((void *)(C64_MEMORY_BASE + 0x0400), 0x00, 0xFC00);

	// copy sid header into C64 memory
	memcpy((void *)(C64_MEMORY_BASE + player), sid_header, 0x80);

	// load file in C64 memory
	file->read((void *)(C64_MEMORY_BASE + start), 65536, &bytes_read);
	printf("Bytes loaded: %d. $%4x-$%4x\n", bytes_read, start, end);

/*
	// clear the other parts of the memory
	int start_d = start & -4;
	int end_d = (end + 3) & -4;
	printf("Clearing memory from $0400, length: %4x\n", (start_d - 0x400));
	printf("Clearing memory from $%4x, length: %4x\n", end_d, 0x10000-end_d);
	
	memset((void *)(C64_MEMORY_BASE + 0x0400), 0x00, (start_d - 0x400));
	memset((void *)(C64_MEMORY_BASE + end_d), 0x00, (0x10000-end_d));
*/

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
    
	c64->resume();

	root.fclose(file);
	file = NULL;

	wait_ms(400);
	c64->set_cartridge(NULL); // reset to default cart
	
	return;
}
