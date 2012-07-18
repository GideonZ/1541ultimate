#include "integer.h"
extern "C" {
	#include "dump_hex.h"
    #include "small_printf.h"
}
#include "iec.h"
#include "iec_channel.h"
#include "poll.h"
#include "c64.h"
#include "filemanager.h"
#include "userinterface.h"

#define MENU_IEC_RESET       0xCA10
#define MENU_IEC_TRACE_ON    0xCA11
#define MENU_IEC_TRACE_OFF   0xCA12

extern BYTE  _binary_iec_code_iec_start;
extern DWORD _binary_iec_code_iec_size;

#define CFG_IEC_ENABLE   0x51
#define CFG_IEC_BUS_ID   0x52

static char *en_dis[] = { "Disabled", "Enabled" };
static struct t_cfg_definition iec_config[] = {
    { CFG_IEC_ENABLE,    CFG_TYPE_ENUM,   "IEC Drive",                 "%s", en_dis,     0,  1, 0 },
    { CFG_IEC_BUS_ID,    CFG_TYPE_VALUE,  "1541 Drive Bus ID",         "%d", NULL,       8, 30, 10 },
    { 0xFF, CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

// this global will cause us to run!
IecInterface iec_if;

// Errors 

// Error,ErrorText(,Track,Sector\015) 
// The track and sector will be send seperately

const CHAR msg00[] = " OK";						//00
const CHAR msg01[] = "FILES SCRATCHED";			//01	Track number shows how many files were removed
const CHAR msg20[] = "READ ERROR"; 				//20 (Block Header Not Found)
//const CHAR msg21[] = "READ ERROR"; 				//21 (No Sync Character)
//const CHAR msg22[] = "READ ERROR"; 				//22 (Data Block not Present)
//const CHAR msg23[] = "READ ERROR"; 				//23 (Checksum Error in Data Block)
//const CHAR msg24[] = "READ ERROR"; 				//24 (Byte Decoding Error)
const CHAR msg25[] = "WRITE ERROR";				//25 (Write/Verify Error)
const CHAR msg26[] = "WRITE PROTECT ON";		//26
//const CHAR msg27[] = "READ ERROR"; 				//27 (Checksum Error in Header)
//const CHAR msg28[] = "WRITE ERROR"; 			//28 (Long Data Block)
const CHAR msg29[] = "DISK ID MISMATCH";		//29
const CHAR msg30[] = "SYNTAX ERROR";			//30 (General)
const CHAR msg31[] = "SYNTAX ERROR";			//31 (Invalid Command)
const CHAR msg32[] = "SYNTAX ERROR";			//32 (Command Line > 58 Characters)
const CHAR msg33[] = "SYNTAX ERROR";			//33 (Invalid Filename)
const CHAR msg34[] = "SYNTAX ERROR";			//34 (No File Given)
const CHAR msg39[] = "SYNTAX ERROR";			//39 (Invalid Command)
const CHAR msg50[] = "RECORD NOT PRESENT";		//50
const CHAR msg51[] = "OVERFLOW IN RECORD";		//51
//const CHAR msg52[] = "FILE TOO LARGE";			//52
const CHAR msg60[] = "WRITE FILE OPEN";			//60
const CHAR msg61[] = "FILE NOT OPEN";			//61
const CHAR msg62[] = "FILE NOT FOUND";			//62
const CHAR msg63[] = "FILE EXISTS";				//63
const CHAR msg64[] = "FILE TYPE MISMATCH";		//64
//const CHAR msg65[] = "NO BLOCK";				//65
//const CHAR msg66[] = "ILLEGAL TRACK AND SECTOR";//66
//const CHAR msg67[] = "ILLEGAL SYSTEM T OR S";	//67
const CHAR msg70[] = "NO CHANNEL";	            //70
const CHAR msg71[] = "DIRECTORY ERROR";			//71
const CHAR msg72[] = "DISK FULL";				//72
const CHAR msg73[] = "ULTIMATE IEC DOS V0.5";	//73 DOS MISMATCH(Returns DOS Version)
const CHAR msg74[] = "DRIVE NOT READY";			//74

const CHAR msg_c1[] = "BAD COMMAND";			//custom
const CHAR msg_c2[] = "UNIMPLEMENTED";			//custom


const IEC_ERROR_MSG last_error_msgs[] = {
		{ 00,(CHAR*)msg00,NR_OF_EL(msg00) - 1 },
		{ 01,(CHAR*)msg01,NR_OF_EL(msg01) - 1 },
		{ 20,(CHAR*)msg20,NR_OF_EL(msg20) - 1 },
//		{ 21,(CHAR*)msg21,NR_OF_EL(msg21) - 1 },
//		{ 22,(CHAR*)msg22,NR_OF_EL(msg22) - 1 },
//		{ 23,(CHAR*)msg23,NR_OF_EL(msg23) - 1 },
//		{ 24,(CHAR*)msg24,NR_OF_EL(msg24) - 1 },
		{ 25,(CHAR*)msg25,NR_OF_EL(msg25) - 1 },
		{ 26,(CHAR*)msg26,NR_OF_EL(msg26) - 1 },
//		{ 27,(CHAR*)msg27,NR_OF_EL(msg27) - 1 },
//		{ 28,(CHAR*)msg28,NR_OF_EL(msg28) - 1 },
		{ 29,(CHAR*)msg29,NR_OF_EL(msg29) - 1 },
		{ 30,(CHAR*)msg30,NR_OF_EL(msg30) - 1 },
		{ 31,(CHAR*)msg31,NR_OF_EL(msg31) - 1 },
		{ 32,(CHAR*)msg32,NR_OF_EL(msg32) - 1 },
		{ 33,(CHAR*)msg33,NR_OF_EL(msg33) - 1 },
		{ 34,(CHAR*)msg34,NR_OF_EL(msg34) - 1 },
		{ 39,(CHAR*)msg39,NR_OF_EL(msg39) - 1 },
		{ 50,(CHAR*)msg50,NR_OF_EL(msg50) - 1 },
		{ 51,(CHAR*)msg51,NR_OF_EL(msg51) - 1 },
//		{ 52,(CHAR*)msg52,NR_OF_EL(msg52) - 1 },
		{ 60,(CHAR*)msg60,NR_OF_EL(msg60) - 1 },
		{ 61,(CHAR*)msg61,NR_OF_EL(msg61) - 1 },
		{ 62,(CHAR*)msg62,NR_OF_EL(msg62) - 1 },
		{ 63,(CHAR*)msg63,NR_OF_EL(msg63) - 1 },
		{ 64,(CHAR*)msg64,NR_OF_EL(msg64) - 1 },
//		{ 65,(CHAR*)msg65,NR_OF_EL(msg65) - 1 },
//		{ 66,(CHAR*)msg66,NR_OF_EL(msg66) - 1 },
//		{ 67,(CHAR*)msg67,NR_OF_EL(msg67) - 1 },
		{ 70,(CHAR*)msg70,NR_OF_EL(msg70) - 1 },
		{ 71,(CHAR*)msg71,NR_OF_EL(msg71) - 1 },
		{ 72,(CHAR*)msg72,NR_OF_EL(msg72) - 1 },
		{ 73,(CHAR*)msg73,NR_OF_EL(msg73) - 1 },
		{ 74,(CHAR*)msg74,NR_OF_EL(msg74) - 1 },

		{ 75,(CHAR*)msg_c1,NR_OF_EL(msg_c1) - 1 },
		{ 76,(CHAR*)msg_c2,NR_OF_EL(msg_c2) - 1 }
};



void poll_iec_interface(Event &ev)
{
    iec_if.poll(ev);
}

IecInterface :: IecInterface()
{
    if(!(CAPABILITIES & CAPAB_HARDWARE_IEC))
        return;

    poll_list.append(&poll_iec_interface);
	main_menu_objects.append(this);
    register_store(0x49454300, "Software IEC Settings", iec_config);

    HW_IEC_RESET_ENABLE = 0; // disable

    int size = (int)&_binary_iec_code_iec_size;
    printf("IEC Processor found: Version = %b. Loading code...", HW_IEC_VERSION);
    BYTE *src = &_binary_iec_code_iec_start;
    BYTE *dst = (BYTE *)HW_IEC_CODE_BE;
    for(int i=0;i<size;i++)
        *(dst++) = *(src++);
    printf("%d bytes loaded.\n", size);

    atn = false;
    path = new Path; // starts in SdCard root ;)

    for(int i=0;i<15;i++) {
        channels[i] = new IecChannel(this, i);
    }
    channels[15] = new IecCommandChannel(this, 15);
    
    current_channel = 0;
    talking = false;
    last_addr = 10;
    last_error = ERR_DOS;

    effectuate_settings();
}

IecInterface :: ~IecInterface()
{
    if(!(CAPABILITIES & CAPAB_HARDWARE_IEC))
        return;

    for(int i=0;i<16;i++)
        delete channels[i];
    delete path;
	poll_list.remove(&poll_iec_interface);
}

void IecInterface :: effectuate_settings(void)
{
    DWORD was_talk   = 0x18800040 + last_addr; // compare instruction
    DWORD was_listen = 0x18800020 + last_addr;
    
//            data = (0x08 << 20) + (bit << 24) + (inv << 29) + (addr << 8) + (value << 0)
    int bus_id = cfg->get_value(CFG_IEC_BUS_ID);
    if(bus_id != last_addr) {
        printf("Setting IEC bus ID to %d.\n", bus_id);
        int replaced = 0;
        for(int i=0;i<512;i++) {
            if ((HW_IEC_RAM_DW[i] & 0x1F8000FF) == was_listen) {
                // printf("Replacing %8x with %8x at %d.\n", HW_IEC_RAM_DW[i], (HW_IEC_RAM_DW[i] & 0xFFFFFF00) + bus_id + 0x20, i);
                HW_IEC_RAM_DW[i] = (HW_IEC_RAM_DW[i] & 0xFFFFFF00) + bus_id + 0x20;
                replaced ++;
            }
            if ((HW_IEC_RAM_DW[i] & 0x1F8000FF) == was_talk) {
                HW_IEC_RAM_DW[i] = (HW_IEC_RAM_DW[i] & 0xFFFFFF00) + bus_id + 0x40;
                replaced ++;
            }
        }  
        //printf("Replaced: %d words.\n", replaced);
        last_addr = bus_id;    
    }
    iec_enable = BYTE(cfg->get_value(CFG_IEC_ENABLE));
    HW_IEC_RESET_ENABLE = iec_enable;
}
    

int IecInterface :: fetch_task_items(IndexedList<PathObject *> &list)
{
	list.append(new ObjectMenuItem(this, "Reset IEC",      MENU_IEC_RESET));
    if(!(CAPABILITIES & CAPAB_ANALYZER))
        return 1;

	list.append(new ObjectMenuItem(this, "Trace IEC",      MENU_IEC_TRACE_ON));
    list.append(new ObjectMenuItem(this, "Dump IEC Trace", MENU_IEC_TRACE_OFF));
	return 3;
}

//BYTE dummy_prg[] = { 0x01, 0x08, 0x0C, 0x08, 0xDC, 0x07, 0x99, 0x22, 0x48, 0x4F, 0x49, 0x22, 0x00, 0x00, 0x00 };

int IecInterface :: poll(Event &e)
{
    int a;
    BYTE data;
    while (!((a = HW_IEC_RX_FIFO_STATUS) & IEC_FIFO_EMPTY)) {
        data = HW_IEC_RX_DATA;
        if(a & IEC_FIFO_CTRL) {
            switch(data) {
                case 0x43:
                    printf("{tlk} ");
                    talking = true;
                    break;
                case 0x45:
                    printf("{end} ");
                    channels[current_channel]->push_command(0);
                    break;
                case 0x41:
                    atn = true;
                    talking = false;
                    printf("<1>", data);
                    break;
                case 0x42:
                    atn = false;
                    printf("<0> ", data);
                    break;
                default:
                    printf("<%b> ", data);
            }
        } else {
            if(atn) {
                printf("[/%b] ", data);
                if(data >= 0x60) { // workaround for passing of wrong atn codes talk/untalk
                    current_channel = int(data & 0x0F);
                    channels[current_channel]->push_command(data & 0xF0);
                }
            } else {
                printf("[%b] ", data);
                channels[current_channel]->push_data(data);
            }
        }
    }

    int st;
    if(talking) {
        if(!(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)) {
            st = channels[current_channel]->pop_data(data);
            if(st == IEC_OK)
                HW_IEC_TX_DATA = data;
            else if(st == IEC_LAST) {
                HW_IEC_TX_LAST = 1;
                while(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)
                    ;
                HW_IEC_TX_DATA = data;
                talking = false;
            } else { 
                printf("Talk Error = %d\n", st);
                HW_IEC_TX_LAST = 0x10;
                talking = false;
            }
        }
    }

	File *f;
	PathObject *po;
	UINT transferred;

	if((e.type == e_object_private_cmd)&&(e.object == this)) {
		switch(e.param) {
            case MENU_IEC_RESET:
                HW_IEC_RESET_ENABLE = iec_enable;
                break;
            case MENU_IEC_TRACE_ON :
                LOGGER_COMMAND = LOGGER_CMD_START;
                start_address = (LOGGER_ADDRESS & 0xFFFFFFFCL);
                printf("Logic Analyzer started. Address = %p. Length=%b\n", start_address, LOGGER_LENGTH);
                break;
            case MENU_IEC_TRACE_OFF:
                LOGGER_COMMAND = LOGGER_CMD_STOP;
                end_address = LOGGER_ADDRESS;
                printf("Logic Analyzer stopped. Address = %p\n", end_address);
                if(start_address == end_address)
                    break;
                po = user_interface->get_path();
                f = root.fcreate("iectrace.bin", po);
                if(f) {
                    printf("Opened file successfully.\n");
                    f->write((void *)start_address, end_address - start_address, &transferred);
                    printf("written: %d...", transferred);
                    f->close();
                } else {
                    printf("Couldn't open file..\n");
                }
                break;
            default:
                break;
        }
    }
}

int IecInterface :: get_last_error(char *buffer)
{
	int len;
	for(int i = 0; i < NR_OF_EL(last_error_msgs); i++) {
		if(last_error == last_error_msgs[i].nr) {
			return sprintf(buffer,"%02d,%s,%02d,00\015", last_error,last_error_msgs[i].msg, 0);
		}
	}
    return sprintf(buffer,"99,UNKNOWN,00,00\015");
}

// tester instance

/*********************************************************************/
/* IEC File Browser Handling                                         */
/*********************************************************************/
#define IECFILE_LOAD 0xCA01

FileTypeIEC tester_iec(file_type_factory);

FileTypeIEC :: FileTypeIEC(FileTypeFactory &fac) : FileDirEntry(NULL, (FileInfo *)NULL)
{
    fac.register_type(this);
}

FileTypeIEC :: FileTypeIEC(PathObject *par, FileInfo *fi) : FileDirEntry(par, fi)
{
}

FileTypeIEC :: ~FileTypeIEC()
{
}

int   FileTypeIEC :: fetch_context_items(IndexedList<PathObject *> &list)
{
	list.append(new MenuItem(this, "Load Code", IECFILE_LOAD));
    return 1+ (FileDirEntry :: fetch_context_items_actual(list));
}

FileDirEntry *FileTypeIEC :: test_type(PathObject *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "IEC")==0)
        return new FileTypeIEC(obj->parent, inf);
    return NULL;
}

void FileTypeIEC :: execute(int selection)
{
    File *file;
    UINT bytes_read;
    
	switch(selection) {
        case IECFILE_LOAD:
            printf("Loading IEC file.\n");
    		file = root.fopen(this, FA_READ);
    		if(file) {
                HW_IEC_RESET_ENABLE = 0;
                file->read((void *)HW_IEC_CODE_BE, 2048, &bytes_read);
                printf("Read %d code bytes.\n", bytes_read);
                HW_IEC_RESET_ENABLE = iec_if.iec_enable;
            }
            break;
        default:
            break;
    }
}
