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
#include "disk_image.h"
#include "pattern.h"

#define MENU_IEC_RESET       0xCA10
#define MENU_IEC_TRACE_ON    0xCA11
#define MENU_IEC_TRACE_OFF   0xCA12
#define MENU_IEC_WARP_8      0xCA13
#define MENU_IEC_WARP_9      0xCA14
#define MENU_IEC_MASTER_1    0xCA16
#define MENU_IEC_MASTER_2    0xCA17
#define MENU_IEC_MASTER_3    0xCA18
#define MENU_IEC_MASTER_4    0xCA19
#define MENU_IEC_LOADDIR     0xCA1A
#define MENU_IEC_LOADFIRST   0xCA1B
#define MENU_READ_STATUS     0xCA1C
#define MENU_SEND_COMMAND    0xCA1D
   
cart_def warp_cart  = { 0x00, (void *)0, 0x1000, 0x01 | CART_REU | CART_RAM };

extern uint8_t  _binary_iec_code_b_start;
extern uint32_t _binary_iec_code_b_size;
//extern BYTE _binary_warp_rom_65_start;
extern uint8_t _binary_ulticopy_65_start;
extern uint32_t _binary_ulticopy_65_size;

#define CFG_IEC_ENABLE   0x51
#define CFG_IEC_BUS_ID   0x52

static const char *en_dis[] = { "Disabled", "Enabled" };
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

const char msg00[] = " OK";						//00
const char msg01[] = "FILES SCRATCHED";			//01	Track number shows how many files were removed
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
//const char msg65[] = "NO BLOCK";				//65
//const char msg66[] = "ILLEGAL TRACK AND SECTOR";//66
//const char msg67[] = "ILLEGAL SYSTEM T OR S";	//67
const char msg70[] = "NO CHANNEL";	            //70
const char msg71[] = "DIRECTORY ERROR";			//71
const char msg72[] = "DISK FULL";				//72
const char msg73[] = "ULTIMATE IEC DOS V0.7";	//73 DOS MISMATCH(Returns DOS Version)
const char msg74[] = "DRIVE NOT READY";			//74

const char msg_c1[] = "BAD COMMAND";			//custom
const char msg_c2[] = "UNIMPLEMENTED";			//custom


const IEC_ERROR_MSG last_error_msgs[] = {
		{ 00,(char*)msg00,NR_OF_EL(msg00) - 1 },
		{ 01,(char*)msg01,NR_OF_EL(msg01) - 1 },
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
//		{ 65,(char*)msg65,NR_OF_EL(msg65) - 1 },
//		{ 66,(char*)msg66,NR_OF_EL(msg66) - 1 },
//		{ 67,(char*)msg67,NR_OF_EL(msg67) - 1 },
		{ 70,(char*)msg70,NR_OF_EL(msg70) - 1 },
		{ 71,(char*)msg71,NR_OF_EL(msg71) - 1 },
		{ 72,(char*)msg72,NR_OF_EL(msg72) - 1 },
		{ 73,(char*)msg73,NR_OF_EL(msg73) - 1 },
		{ 74,(char*)msg74,NR_OF_EL(msg74) - 1 },

		{ 75,(char*)msg_c1,NR_OF_EL(msg_c1) - 1 },
		{ 76,(char*)msg_c2,NR_OF_EL(msg_c2) - 1 }
};



void IecInterface :: poll_iec_interface(Event &ev)
{
    iec_if.poll(ev);
}

IecInterface :: IecInterface() : SubSystem(SUBSYSID_IEC)
{
	fm = FileManager :: getFileManager();
	ui_window = NULL;
    
    if(!(getFpgaCapabilities() & CAPAB_HARDWARE_IEC))
        return;

    MainLoop :: addPollFunction(poll_iec_interface);
    register_store(0x49454300, "Software IEC Settings", iec_config);

    HW_IEC_RESET_ENABLE = 0; // disable

    int size = (int)&_binary_iec_code_b_size;
    printf("IEC Processor found: Version = %b. Loading code...", HW_IEC_VERSION);
    uint8_t *src = &_binary_iec_code_b_start;
    uint8_t *dst = (uint8_t *)HW_IEC_CODE;
    for(int i=0;i<size;i++)
        *(dst++) = *(src++);
    printf("%d bytes loaded.\n", size);

    printf("Word 0: %8x\n", HW_IEC_RAM_DW[0]);
    printf("Word 1: %8x\n", HW_IEC_RAM_DW[1]);
    printf("Word 2: %8x\n", HW_IEC_RAM_DW[2]);

    atn = false;
    path = fm->get_new_path("IEC");
    path->cd("SD");
    cmd_path = fm->get_new_path("IEC Gui Path");
    cmd_ui = 0;

    dirlist = new IndexedList<FileInfo *>(8, NULL);
	iecNames = new IndexedList<char *>(8, NULL);
    last_error = ERR_DOS;
    current_channel = 0;
    talking = false;
    last_addr = 10;
    wait_irq = false;

    effectuate_settings();

    for(int i=0;i<15;i++) {
        channels[i] = new IecChannel(this, i);
    }
    channels[15] = new IecCommandChannel(this, 15);
}

IecInterface :: ~IecInterface()
{
    if(!(getFpgaCapabilities() & CAPAB_HARDWARE_IEC))
        return;

    for(int i=0;i<16;i++)
        delete channels[i];
    fm->release_path(path);
    fm->release_path(cmd_path);
    MainLoop :: removePollFunction(poll_iec_interface);
}

void IecInterface :: effectuate_settings(void)
{
    uint32_t was_talk   = 0x18800040 + last_addr; // compare instruction
    uint32_t was_listen = 0x18800020 + last_addr;
    
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
        printf("Replaced: %d words.\n", replaced);
        last_addr = bus_id;    
    }
    iec_enable = uint8_t(cfg->get_value(CFG_IEC_ENABLE));
    HW_IEC_RESET_ENABLE = iec_enable;
}
    

int IecInterface :: fetch_task_items(IndexedList<Action *> &list)
{
    int count = 3;
	list.append(new Action("Reset IEC",      SUBSYSID_IEC, MENU_IEC_RESET));
	list.append(new Action("UltiCopy 8",     SUBSYSID_IEC, MENU_IEC_WARP_8));
	list.append(new Action("UltiCopy 9",     SUBSYSID_IEC, MENU_IEC_WARP_9));
	// list.append(new Action("IEC Test 1",     SUBSYSID_IEC, MENU_IEC_MASTER_1));
	// list.append(new Action("IEC Test 2",     SUBSYSID_IEC, MENU_IEC_MASTER_2));
	// list.append(new Action("IEC Test 3",     SUBSYSID_IEC, MENU_IEC_MASTER_3));
	// list.append(new Action("IEC Test 4",     SUBSYSID_IEC, MENU_IEC_MASTER_4));
    // list.append(new Action("Load $",         SUBSYSID_IEC, MENU_IEC_LOADDIR));
    // list.append(new Action("Load *",         SUBSYSID_IEC, MENU_IEC_LOADFIRST));
    // list.append(new Action("Read status",    SUBSYSID_IEC, MENU_READ_STATUS));
    // list.append(new Action("Send command",   SUBSYSID_IEC, MENU_SEND_COMMAND));

    if(!(getFpgaCapabilities() & CAPAB_ANALYZER))
        return count;

	list.append(new Action("Trace IEC",      SUBSYSID_IEC, MENU_IEC_TRACE_ON));
    list.append(new Action("Dump IEC Trace", SUBSYSID_IEC, MENU_IEC_TRACE_OFF));
    count += 2;
	return count;
}

//BYTE dummy_prg[] = { 0x01, 0x08, 0x0C, 0x08, 0xDC, 0x07, 0x99, 0x22, 0x48, 0x4F, 0x49, 0x22, 0x00, 0x00, 0x00 };

int IecInterface :: poll(Event &e)
{
    uint8_t data;

    if(wait_irq) {
        if (HW_IEC_IRQ & 0x01) {
            get_warp_data();
        }
        return 0;
    }
    uint8_t a;
    while (!((a = HW_IEC_RX_FIFO_STATUS) & IEC_FIFO_EMPTY)) {
        data = HW_IEC_RX_DATA;
        if(a & IEC_FIFO_CTRL) {
            switch(data) {
                case 0xDA:
                    HW_IEC_TX_DATA = 0x00; // handshake and wait for IRQ
                    wait_irq = true;
                    return 0;
                case 0xAD:
                    ioWrite8(UART_DATA, 0x23); // printf("{warp_end}");
                    break;
                case 0xDE:
                    get_warp_error();
                    break;
                case 0x57:
                    printf("{warp mode}");
                    break;
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
        while(!(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)) {
        	st = channels[current_channel]->pop_data(data);
            if(st == IEC_OK)
                HW_IEC_TX_DATA = data;
            else if(st == IEC_LAST) {
                HW_IEC_TX_CTRL = 1;
                while(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)
                    ;
                HW_IEC_TX_DATA = data;
                talking = false;
                break;
            } else { 
                printf("Talk Error = %d\n", st);
                HW_IEC_TX_CTRL = 0x10;
                talking = false;
                break;
            }
        }
    }
}

int IecInterface :: executeCommand(SubsysCommand *cmd)
{
	File *f;
	uint32_t transferred;
    char buffer[24];
    int res;

    cmd_path->cd(cmd->path.c_str());
    cmd_ui = cmd->user_interface;

	switch(cmd->functionID) {
		case MENU_IEC_RESET:
			HW_IEC_RESET_ENABLE = iec_enable;
			break;
		case MENU_IEC_WARP_8:
			start_warp(8);
			break;
		case MENU_IEC_WARP_9:
			start_warp(9);
			break;
		case MENU_IEC_MASTER_1:
			test_master(1);
			break;
		case MENU_IEC_MASTER_2:
			test_master(2);
			break;
		case MENU_IEC_MASTER_3:
			test_master(3);
			break;
		case MENU_IEC_MASTER_4:
			test_master(4);
			break;
		case MENU_IEC_LOADDIR:
			master_open_file(8, 0, "$", false);
			break;
		case MENU_IEC_LOADFIRST:
			master_open_file(8, 0, "*", false);
			break;
		case MENU_READ_STATUS:
			master_read_status(8);
			break;
		case MENU_SEND_COMMAND:
			buffer[0] = 0;
			res = cmd->user_interface->string_box("Command", buffer, 22);
			if (res > 0) {
				master_send_cmd(8, (uint8_t*)buffer, strlen(buffer));
			}
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
			f = fm->fopen(cmd->path.c_str(), "iectrace.bin", FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS); // TODO: Path
			if(f) {
				printf("Opened file successfully.\n");
				f->write((void *)start_address, end_address - start_address, &transferred);
				printf("written: %d...", transferred);
				fm->fclose(f);
			} else {
				printf("Couldn't open file..\n");
			}
			break;
		default:
			break;
    }
    return 0;
}

#include "c1541.h"
extern C1541 *c1541_A;
extern C1541 *c1541_B;

void IecInterface :: start_warp(int drive)
{
    warp_drive = drive;
    printf("Starting IEC Warp Mode.\n");
    C64_POKE(0xDD00,0x07); // make sure the C64 that might be connected does not interfere
    // make sure that our local drives are turned off as well
    if (c1541_A) {
        c1541_A->drive_power(false);
    }
    if (c1541_B) {
        c1541_B->drive_power(false);
    }
    
    ui_window = new UltiCopy();
    ui_window->init(cmd_ui->screen, cmd_ui->keyboard);
    cmd_ui->activate_uiobject(ui_window); // now we have focus
    ui_window->window->move_cursor(15,10);
    ui_window->window->output("Loading...");
    HW_IEC_RESET_ENABLE = 1; // reset the IEC controller, just in case
    
    if(!run_drive_code(warp_drive, 0x400, &_binary_ulticopy_65_start, (int)&_binary_ulticopy_65_size)) {
        cmd_ui->popup("Error accessing drive..", BUTTON_OK);
        ui_window->close();
        push_event(e_refresh_browser);
        if (c1541_A) {
            c1541_A->effectuate_settings();
        }
        if (c1541_B) {
            c1541_B->effectuate_settings();
        }
        HW_IEC_RESET_ENABLE = iec_enable;
        return;
    }

    ui_window->window->clear();
    HW_IEC_RESET_ENABLE = 1; // reset the IEC controller, just in case
    // clear pending interrupt if any
    HW_IEC_IRQ = 0;
    // push warp command into down-fifo
    HW_IEC_TX_CTRL = IEC_CMD_GO_WARP;
    last_track = 0;
}

void IecInterface :: get_warp_data(void)
{
    uint32_t read;
    uint8_t temp[260];
    uint32_t *dw = (uint32_t *)&temp[0];
    int err = 0;
    for(int i=0;i<64;i++) {
        GCR_DECODER_GCR_IN_32 = HW_IEC_RX_DATA_32; // first in first out, endianness OK
        GCR_DECODER_GCR_IN = HW_IEC_RX_DATA;
        *(dw++) = GCR_DECODER_BIN_OUT_32;
        if(GCR_DECODER_ERRORS)
            err++;
    }
    read = HW_IEC_RX_DATA_32;
    GCR_DECODER_GCR_IN_32 = read;
    GCR_DECODER_GCR_IN = 0x55;
    *(dw++) = GCR_DECODER_BIN_OUT_32;

    uint8_t sector = (uint8_t)read;
    uint8_t track = HW_IEC_RX_DATA;
    uint8_t *dest = static_bin_image.get_sector_pointer(track, sector);
    uint8_t *src = &temp[1];
    // printf("Sector {%b %b (%p -> %p}\n", track, sector, src, dest);
    ui_window->window->set_char(track-1,sector+1,err?'-':'*');
    last_track = track;
    if(dest) {
        for(int i=0;i<256;i++) {
            *(dest++) = *(src++);
        }
    }
    // clear pending interrupt
    wait_irq = false;
    HW_IEC_IRQ = 0;
}

void IecInterface :: get_warp_error(void)
{
    while (HW_IEC_RX_FIFO_STATUS & IEC_FIFO_EMPTY)
        ;
        
    uint8_t err = HW_IEC_RX_DATA;
    printf("{Warp Error: %b}", err);
    // clear pending interrupt
    HW_IEC_IRQ = 0;
    bool re_enable = false;

    if(err & 0x80) {
        last_track++;
        printf("Error on track %d.\n", last_track);
    } else if(err == 0) {
        save_copied_disk();
        if(cmd_ui->popup("Another disk?", BUTTON_YES|BUTTON_NO) == BUTTON_YES) {
            ui_window->window->clear();
            run_drive_code(warp_drive, 0x403, NULL, 0); // restart
            // clear pending interrupt if any
            HW_IEC_IRQ = 0;
            // push warp command into down-fifo
            HW_IEC_TX_CTRL = IEC_CMD_GO_WARP;
            last_track = 0;
        } else {
            ui_window->close();
            push_event(e_reload_browser);
            re_enable = true;
        }
    } else if(err < 0x20) {
        cmd_ui->popup("Error reading disk..", BUTTON_OK);
        ui_window->close();
        re_enable = true;
    }
    
    if(re_enable) {
        if (c1541_A) {
            c1541_A->effectuate_settings();
        }
        if (c1541_B) {
            c1541_B->effectuate_settings();
        }
        HW_IEC_RESET_ENABLE = iec_enable;
    }    
}

void IecInterface :: save_copied_disk()
{
    char buffer[40];
    int save_result;
    File *f;
    int res;
    BinImage *bin;
    CachedTreeNode *po;
    
    static_bin_image.num_tracks = 35; // standard!

	// buffer[0] = 0;
	static_bin_image.get_sensible_name(buffer);
	res = cmd_ui->string_box("Give name for copied disk..", buffer, 22);
	if(res > 0) {
		fix_filename(buffer);
	    bin = &static_bin_image;
		set_extension(buffer, ".d64", 32);
        f = fm->fopen(cmd_path, buffer, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS);
		if(f) {
            cmd_ui->show_progress("Saving D64..", 35);
            save_result = bin->save(f, cmd_ui);
            cmd_ui->hide_progress();
    		printf("Result of save: %d.\n", save_result);
            fm->fclose(f);
    		push_event(e_reload_browser);
		} else {
			printf("Can't create file '%s'\n", buffer);
			cmd_ui->popup("Can't create file.", BUTTON_OK);
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

void IecInterface :: master_open_file(int device, int channel, char *filename, bool write)
{
    printf("Open '%s' on device '%d', channel %d\n", filename, device, channel);
    HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
    HW_IEC_TX_DATA = 0x20 | (((uint8_t)device) & 0x1F); // listen!
    HW_IEC_TX_DATA = 0xF0 | (((uint8_t)channel) & 0x0F); // open on channel x
    HW_IEC_TX_CTRL = IEC_CMD_ATN_TO_TX;

    int len = strlen(filename);
    for (int i=0;i<len;i++) {
        while(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)
            ;
        if (i == (len-1))
            HW_IEC_TX_CTRL = 0x01; // EOI
        while(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)
            ;
        HW_IEC_TX_DATA = (uint8_t)filename[i];
    }
    if(!write) {
        while(!(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_EMPTY))
            ;
        HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
        HW_IEC_TX_DATA = 0x3F; // unlisten
        HW_IEC_TX_CTRL = IEC_CMD_ATN_RELEASE;
        // and talk!
        HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
        HW_IEC_TX_DATA = 0x40 | (((uint8_t)device) & 0x1F); // talk
        HW_IEC_TX_DATA = 0x60 | (((uint8_t)channel) & 0x0F); // open channel
        HW_IEC_TX_CTRL = IEC_CMD_ATN_TO_RX;
    }
}        

bool IecInterface :: master_send_cmd(int device, uint8_t *cmd, int length)
{
    //printf("Send command on device '%d' [%s]\n", device, cmd);
    // dump_hex(cmd, length);
    
    HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
    HW_IEC_TX_DATA = 0x20 | (((uint8_t)device) & 0x1F); // listen!
    HW_IEC_TX_DATA = 0x6F;
    HW_IEC_TX_CTRL = IEC_CMD_ATN_TO_TX;

    for (int i=0;i<length;i++) {
        while(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)
            ;
        if (i == (length-1))
            HW_IEC_TX_CTRL = 0x01; // EOI
        while(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)
            ;
        HW_IEC_TX_DATA = cmd[i];
    }
    while(!(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_EMPTY))
        ;

    HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
    HW_IEC_TX_DATA = 0x3F; // unlisten
    HW_IEC_TX_CTRL = IEC_CMD_ATN_RELEASE;

    while(!(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_EMPTY))
        ;

    uint8_t st,code;
    st = HW_IEC_RX_FIFO_STATUS;
    if(!(st & IEC_FIFO_EMPTY)) {
        code = HW_IEC_RX_DATA;
        if(st & IEC_FIFO_CTRL) {
            printf("Return code: %b\n", code);
            if((code & 0xF0) == 0xE0)
                return false;
        } else {
            printf("Huh? Didn't expect data: %b\n", code);
        }        
    }
    return true;
}

void IecInterface :: master_read_status(int device)
{
    printf("Reading status channel from device %d\n", device);
    HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
    HW_IEC_TX_DATA = 0x40 | (((uint8_t)device) & 0x1F); // listen!
    HW_IEC_TX_DATA = 0x6F; // channel 15
    HW_IEC_TX_CTRL = IEC_CMD_ATN_TO_RX;
}

bool IecInterface :: run_drive_code(int device, uint16_t addr, uint8_t *code, int length)
{
    printf("Load drive code. Length = %d\n", length);
    uint8_t buffer[40];
    uint16_t address = addr;
    int size;
    strcpy((char*)buffer, "M-W");
    while(length > 0) {
        size = (length > 32)?32:length;
        buffer[3] = (uint8_t)(address & 0xFF);
        buffer[4] = (uint8_t)(address >> 8);
        buffer[5] = (uint8_t)(size);
        for(int i=0;i<size;i++) {
            buffer[6+i] = *(code++);
        }
        printf(".");
        if(!master_send_cmd(device, buffer, size+6))
            return false;
        length -= size;
        address += size;
    }
    strcpy((char*)buffer, "M-E");
    buffer[3] = (uint8_t)(addr & 0xFF);
    buffer[4] = (uint8_t)(addr >> 8);
    return master_send_cmd(device, buffer, 5);
}

void IecInterface :: test_master(int test)
{
    switch(test) {
    case 1:
        printf("Open $ on device 8\n");
        HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
        HW_IEC_TX_DATA = 0x28; // listen #8!
        HW_IEC_TX_DATA = 0xF0; // open
        HW_IEC_TX_CTRL = IEC_CMD_ATN_TO_TX;
        HW_IEC_TX_CTRL = 0x01; // EOI
        HW_IEC_TX_DATA = '$';
        HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
        HW_IEC_TX_DATA = 0x3F; // unlisten
        HW_IEC_TX_CTRL = IEC_CMD_ATN_RELEASE;
        break;
    case 2:
        printf("Talk channel 0\n");
        HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
        HW_IEC_TX_DATA = 0x48; // talk #8!!
        HW_IEC_TX_DATA = 0x60; // channel 0
        HW_IEC_TX_CTRL = IEC_CMD_ATN_TO_RX;
        break;
    case 3:
        printf("Close file on 8\n");
        HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
        HW_IEC_TX_DATA = 0x5F; // untalk
        HW_IEC_TX_CTRL = IEC_CMD_ATN_RELEASE;
        HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
        HW_IEC_TX_DATA = 0x28; // listen #8!
        HW_IEC_TX_DATA = 0xE0; // close
        HW_IEC_TX_DATA = 0x3F; // unlisten
        HW_IEC_TX_CTRL = IEC_CMD_ATN_RELEASE;
        break;
    case 4:
        printf("Reading status channel\n");
        HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
        HW_IEC_TX_DATA = 0x48; // talk #8!!
        HW_IEC_TX_DATA = 0x6F; // channel 15
        HW_IEC_TX_CTRL = IEC_CMD_ATN_TO_RX;
        break;        
    default:
        printf("To be defined.\n");
    }
}

void IecInterface :: cleanupDir() {
	if (!dirlist)
		return;
	for(int i=0;i < dirlist->get_elements();i++) {
		delete (*dirlist)[i];
		delete (*iecNames)[i];

	}
	dirlist->clear_list();
	iecNames->clear_list();
}

void IecInterface :: readDirectory()
{
	cleanupDir();
	path->get_directory(*dirlist);
	for(int i=0;i<dirlist->get_elements();i++) {
		iecNames->append(getIecName((*dirlist)[i]->lfname));
	}
}

char *IecInterface :: getIecName(char *in)
{
	char *out = new char[20];
	memset(out, 0, 20);

	for(int i=0;i<16;i++) {
		char o;
		if(in[i] == 0) {
			break;
		} else if(in[i] == '_') {
			o = 164;
		} else if(in[i] < 32) {
			o = 32;
		} else {
			o = toupper(in[i]);
		}
		out[i] = o;
	}
	return out;
}

int IecInterface :: findIecName(char *name, char *ext)
{
	for(int i=0;i<iecNames->get_elements();i++) {
		if (pattern_match(name, (*iecNames)[i], true)) {
			return i;
		}
	}
	return -1;
}

/*********************************************************************/
/* Copier user interface screen
/*********************************************************************/
UltiCopy :: UltiCopy()
{
    parent_win = NULL;
    keyb = NULL;
    window = NULL;
    return_code = 0;
}
    
UltiCopy :: ~UltiCopy()
{
}    

void UltiCopy :: init(Screen *scr, Keyboard *key)
{
    parent_win = scr;
    keyb = key;
    window = new Window(parent_win, 0, 1, 40, 23);
    window->draw_border_horiz();
    window->clear();
}
    
void UltiCopy :: deinit(void)
{
    if (window)
        delete window;
}

int UltiCopy :: handle_key(uint8_t c)
{
	return 0;
}

int UltiCopy :: poll(int a, Event& e)
{
    return return_code;
}
        
void UltiCopy :: close(void)
{
    return_code = 1;
}
    

/*********************************************************************/
/* IEC File Browser Handling                                         */
/*********************************************************************/
/*
#define IECFILE_LOAD 0xCA01

FileTypeIEC tester_iec(file_type_factory);

FileTypeIEC :: FileTypeIEC(CachedTreeNode *par, FileInfo *fi) : FileDirEntry(par, fi)
{
}

FileTypeIEC :: ~FileTypeIEC()
{
}

int   FileTypeIEC :: fetch_context_items(IndexedList<Action *> &list)
{
	list.append(new MenuItem(this, "Load Code", IECFILE_LOAD));
    return 1+ (FileDirEntry :: fetch_context_items_actual(list));
}

FileDirEntry *FileTypeIEC :: test_type(CachedTreeNode *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "IEC")==0)
        return new FileTypeIEC(obj->parent, inf);
    return NULL;
}

void FileTypeIEC :: execute(int selection)
{
    File *file;
    uint32_t bytes_read;
    
	switch(selection) {
        case IECFILE_LOAD:
            printf("Loading IEC file.\n");
    		file = root.fopen(this, FA_READ);
    		if(file) {
                HW_IEC_RESET_ENABLE = 0;
                file->read((void *)HW_IEC_CODE, 2048, &bytes_read);
                printf("Read %d code bytes.\n", bytes_read);
                HW_IEC_RESET_ENABLE = iec_if.iec_enable;
            }
            break;
        default:
            break;
    }
}
*/

