#include "integer.h"
extern "C" {
	#include "dump_hex.h"
    #include "small_printf.h"
}
#include "iec.h"
#include "iec_channel.h"
#include "iec_printer.h"
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
#define MENU_IEC_FLUSH       0xCA1E
   
cart_def warp_cart  = { 0x00, (void *)0, 0x1000, 0x01 | CART_REU | CART_RAM };

extern uint8_t  _iec_code_b_start;
extern uint32_t _iec_code_b_size;
//extern BYTE _warp_rom_65_start;
extern uint8_t _ulticopy_65_start;
extern uint32_t _ulticopy_65_size;

#define CFG_IEC_ENABLE   0x51
#define CFG_IEC_BUS_ID   0x52
#define CFG_IEC_PATH     0x53
#define CFG_IEC_PRINTER_ID 	 0x30
#define CFG_IEC_PRINTER_FILENAME 0x31
#define CFG_IEC_PRINTER_TYPE     0x32
#define CFG_IEC_PRINTER_DENSITY  0x33
#define CFG_IEC_PRINTER_EMULATION  0x34
#define CFG_IEC_PRINTER_CBM_CHAR   0x35
#define CFG_IEC_PRINTER_EPSON_CHAR 0x36
#define CFG_IEC_PRINTER_IBM_CHAR   0x37

static const char *en_dis[] = { "Disabled", "Enabled" };
static const char *pr_typ[] = { "RAW", "PNG" };
static const char *pr_ink[] = { "Low", "Medium", "High" };
static const char *pr_emu[] = { "Commodore MPS", "Epson FX-80", "IBM Graphics Printer", "IBM Proprinter" };
static const char *pr_cch[] = { "USA/UK", "Denmark", "France/Italy", "Germany", "Spain", "Sweden", "Switzerland" };
static const char *pr_ech[] = { "Basic", "USA", "France", "Germany", "UK", "Denmark I",
                                "Sweden", "Italy", "Spain", "Japan", "Norway", "Denmark II" };
static const char *pr_ich[] = { "International 1", "International 2", "Israel", "Greece", "Portugal", "Spain" };

static struct t_cfg_definition iec_config[] = {
    { CFG_IEC_ENABLE,    CFG_TYPE_ENUM,   "IEC Drive and printer",     "%s", en_dis,     0,  1, 0 },
    { CFG_IEC_BUS_ID,    CFG_TYPE_VALUE,  "Soft Drive Bus ID",         "%d", NULL,       8, 30, 10 },
    { CFG_IEC_PATH,      CFG_TYPE_STRING, "Default Path",              "%s", NULL,       0, 30, (int) FS_ROOT },
    { CFG_IEC_PRINTER_ID,       CFG_TYPE_VALUE,  "Printer Bus ID",       "%d", NULL,   4,  5, 4 },
    { CFG_IEC_PRINTER_FILENAME, CFG_TYPE_STRING, "Printer output file",  "%s", NULL,   1, 31, (int) FS_ROOT "printer" },
    { CFG_IEC_PRINTER_TYPE,     CFG_TYPE_ENUM,   "Printer output type",  "%s", pr_typ, 0,  1, 1 },
    { CFG_IEC_PRINTER_DENSITY,  CFG_TYPE_ENUM,   "Printer ink density",  "%s", pr_ink, 0,  2, 1 },
    { CFG_IEC_PRINTER_EMULATION,CFG_TYPE_ENUM,   "Printer emulation",    "%s", pr_emu, 0,  3, 0 },
    { CFG_IEC_PRINTER_CBM_CHAR, CFG_TYPE_ENUM,   "Printer Commodore charset", "%s", pr_cch, 0,  6, 0 },
    { CFG_IEC_PRINTER_EPSON_CHAR,CFG_TYPE_ENUM,  "Printer Epson charset","%s", pr_ech, 0,  11, 0 },
    { CFG_IEC_PRINTER_IBM_CHAR, CFG_TYPE_ENUM,   "Printer IBM table 2",  "%s", pr_ich, 0,  5, 0 },
    { 0xFF, CFG_TYPE_END,    "", "", NULL, 0, 0, 0 }
};

__inline uint32_t swap_if_cpu_is_little_endian(uint32_t a)
{
#ifndef NIOS
	return a;
#else
	uint32_t m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
#endif
}


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



void IecInterface :: iec_task(void *a)
{
	IecInterface *iec = (IecInterface *)a;
	iec->poll();
}

IecInterface :: IecInterface() : SubSystem(SUBSYSID_IEC)
{
	fm = FileManager :: getFileManager();
	ui_window = NULL;
    
    if(!(getFpgaCapabilities() & CAPAB_HARDWARE_IEC))
        return;

    register_store(0x49454300, "Software IEC Settings", iec_config);

    HW_IEC_RESET_ENABLE = 0; // disable

    int size = (int)&_iec_code_b_size;
    printf("IEC Processor found: Version = %b. Loading code...", HW_IEC_VERSION);
    uint8_t *src = &_iec_code_b_start;
    uint8_t *dst = (uint8_t *)HW_IEC_CODE;
    for(int i=0;i<size;i++)
        *(dst++) = *(src++);
    printf("%d bytes loaded.\n", size);

    atn = false;
    path = fm->get_new_path("IEC");
    path->cd(cfg->get_string(CFG_IEC_PATH));
    cmd_path = fm->get_new_path("IEC Gui Path");
    cmd_ui = 0;

    dirlist = new IndexedList<FileInfo *>(8, NULL);
	iecNames = new IndexedList<char *>(8, NULL);
    last_error = ERR_DOS;
    current_channel = 0;
    talking = false;
    last_addr = 10;
    last_printer_addr = 4;
    wait_irq = false;
    printer = false;

    channel_printer = new IecPrinter();

    effectuate_settings();

    for(int i=0;i<15;i++) {
        channels[i] = new IecChannel(this, i);
    }
    channels[15] = new IecCommandChannel(this, 15);
    
    channel_printer->init_done();

    ulticopyBusy = xSemaphoreCreateBinary();
    ulticopyMutex = xSemaphoreCreateMutex();
    queueGuiToIec = xQueueCreate(2, sizeof(int));

    xTaskCreate( IecInterface :: iec_task, "IEC Server", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 2, &taskHandle );
}

IecInterface :: ~IecInterface()
{
    if(!(getFpgaCapabilities() & CAPAB_HARDWARE_IEC))
        return;

    if (taskHandle) {
    	vTaskDelete(taskHandle);
    }
    for(int i=0;i<16;i++)
        delete channels[i];

    delete channel_printer;
    fm->release_path(path);
    fm->release_path(cmd_path);
}

void IecInterface :: effectuate_settings(void)
{
    uint32_t was_talk   = 0x18800040 + last_addr; // compare instruction
    uint32_t was_listen = 0x18800020 + last_addr;
    uint32_t was_printer_listen = 0x18800020 + last_printer_addr;
    
//            data = (0x08 << 20) + (bit << 24) + (inv << 29) + (addr << 8) + (value << 0)
    int bus_id = cfg->get_value(CFG_IEC_BUS_ID);
    if(bus_id != last_addr) {
        printf("Setting IEC bus ID to %d.\n", bus_id);
        int replaced = 0;
        for(int i=0;i<512;i++) {
        	uint32_t word_read = swap_if_cpu_is_little_endian(HW_IEC_RAM_DW[i]);
        	if ((word_read & 0x1F8000FF) == was_listen) {
                // printf("Replacing %8x with %8x at %d.\n", HW_IEC_RAM_DW[i], (HW_IEC_RAM_DW[i] & 0xFFFFFF00) + bus_id + 0x20, i);
                HW_IEC_RAM_DW[i] = swap_if_cpu_is_little_endian((word_read & 0xFFFFFF00) + bus_id + 0x20);
                replaced ++;
            }
            if ((word_read & 0x1F8000FF) == was_talk) {
                HW_IEC_RAM_DW[i] = swap_if_cpu_is_little_endian((word_read & 0xFFFFFF00) + bus_id + 0x40);
                replaced ++;
            }
        }  
        printf("Replaced: %d words.\n", replaced);
        last_addr = bus_id;    
    }
    bus_id = cfg->get_value(CFG_IEC_PRINTER_ID);
    if(bus_id != last_printer_addr) {
        printf("Setting IEC printer ID to %d.\n", bus_id);
        int replaced = 0;
        for(int i=0;i<512;i++) {
        	uint32_t word_read = swap_if_cpu_is_little_endian(HW_IEC_RAM_DW[i]);
            if ((word_read & 0x1F8000FF) == was_printer_listen) {
                HW_IEC_RAM_DW[i] = swap_if_cpu_is_little_endian((word_read & 0xFFFFFF00) + bus_id + 0x20);
                replaced ++;
            }
        } 
        printf("Replaced: %d words.\n", replaced);
        last_printer_addr = bus_id;   
    }

    channel_printer->set_filename(cfg->get_string(CFG_IEC_PRINTER_FILENAME));
    channel_printer->set_output_type(cfg->get_value(CFG_IEC_PRINTER_TYPE));
    channel_printer->set_ink_density(cfg->get_value(CFG_IEC_PRINTER_DENSITY));
    channel_printer->set_emulation(cfg->get_value(CFG_IEC_PRINTER_EMULATION));
    channel_printer->set_cbm_charset(cfg->get_value(CFG_IEC_PRINTER_CBM_CHAR));
    channel_printer->set_epson_charset(cfg->get_value(CFG_IEC_PRINTER_EPSON_CHAR));
    channel_printer->set_ibm_charset(cfg->get_value(CFG_IEC_PRINTER_IBM_CHAR));

    iec_enable = uint8_t(cfg->get_value(CFG_IEC_ENABLE));
    HW_IEC_RESET_ENABLE = iec_enable;
}
    

// called from GUI task
int IecInterface :: fetch_task_items(Path *path, IndexedList<Action *> &list)
{
    int count = 3;
	list.append(new Action("Flush Printer/Eject Page", SUBSYSID_IEC, MENU_IEC_FLUSH));
	list.append(new Action("Reset IEC and Printer",    SUBSYSID_IEC, MENU_IEC_RESET));
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

#ifndef DEVELOPER
	return count;
#endif

	if(!(getFpgaCapabilities() & CAPAB_ANALYZER))
        return count;

	list.append(new Action("Trace IEC",      SUBSYSID_IEC, MENU_IEC_TRACE_ON));
    list.append(new Action("Dump IEC Trace", SUBSYSID_IEC, MENU_IEC_TRACE_OFF));
    count += 2;
	return count;
}

//BYTE dummy_prg[] = { 0x01, 0x08, 0x0C, 0x08, 0xDC, 0x07, 0x99, 0x22, 0x48, 0x4F, 0x49, 0x22, 0x00, 0x00, 0x00 };

// this is actually the task
void IecInterface :: poll()
{
    uint8_t data;
    int command;
    BaseType_t gotSomething;

    while(1) {
    	if(wait_irq) {
			if (HW_IEC_IRQ & 0x01) {
				get_warp_data();
			}
		    continue;
		}
    	gotSomething = xQueueReceive(queueGuiToIec, &command, 2); // here is the vTaskDelay(2) that used to be here
    	if (gotSomething == pdTRUE) {
    		start_warp_iec();
    	}
		uint8_t a;
		while (!((a = HW_IEC_RX_FIFO_STATUS) & IEC_FIFO_EMPTY)) {
			data = HW_IEC_RX_DATA;
			if(a & IEC_FIFO_CTRL) {
				switch(data) {
					case 0xDA:
						HW_IEC_TX_DATA = 0x00; // handshake and wait for IRQ
						wait_irq = true;
						break;
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
						// printf("{tlk} ");
						HW_IEC_TX_FIFO_RELEASE = 1;
						talking = true;
						break;
					case 0x45:
						//printf("{end} ");
						if (printer) {
							channel_printer->push_command(0xFF);
							printer = false;
						} else {
							channels[current_channel]->push_command(0);
						}
						break;
					case 0x41:
						atn = true;
						if (talking) {
							channels[current_channel]->reset_prefetch();
							talking = false;
						}
						//printf("<1>", data);
						break;
					case 0x42:
						atn = false;
						//printf("<0> ", data);
						break;
					case 0x47:
						if (!printer) {
							channels[current_channel]->pop_data();
						}
						break;
					case 0x46:
						printer = true;
						channel_printer->push_command(0xFE);
						break;
					default:
						//printf("<%b> ", data);
						break;
				}
			} else {
				if(atn) {
					// printf("[/%b] ", data);
					if(data >= 0x60) {  // workaround for passing of wrong atn codes talk/untalk
						if (printer) {
							channel_printer->push_command(data & 0x7);
						} else {
							current_channel = int(data & 0x0F);
							channels[current_channel]->push_command(data & 0xF0);
						}
					}
				} else {
					if (printer) {
						channel_printer->push_data(data);
					} else {
						// printf("[%b] ", data);
						channels[current_channel]->push_data(data);
					}
				}
			}
			if (wait_irq) {
				break;
			}
		}

		int st;
		if(talking) {
			while(!(HW_IEC_TX_FIFO_STATUS & IEC_FIFO_FULL)) {
				st = channels[current_channel]->prefetch_data(data);
				if(st == IEC_OK) {
					HW_IEC_TX_DATA = data;
				} else if(st == IEC_LAST) {
					HW_IEC_TX_LAST = data;
					talking = false;
					break;
				} else if(st == IEC_BUFFER_END) {
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
}

// called from GUI task
int IecInterface :: executeCommand(SubsysCommand *cmd)
{
	File *f = 0;
	uint32_t transferred;
    char buffer[24];
    int res;
    FRESULT fres;

    cmd_path->cd(cmd->path.c_str());
    cmd_ui = cmd->user_interface;

	switch(cmd->functionID) {
		case MENU_IEC_RESET:
			channel_printer->reset();
			HW_IEC_RESET_ENABLE = iec_enable;
			last_error = ERR_DOS;
			break;
		case MENU_IEC_FLUSH:
			channel_printer->flush();
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
			fres = fm->fopen(cmd->path.c_str(), "iectrace.bin", FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &f);
			if(f) {
				printf("Opened file successfully.\n");
				f->write((void *)start_address, end_address - start_address, &transferred);
				printf("written: %d...", transferred);
				fm->fclose(f);
			} else {
				printf("Couldn't open file.. %s\n", FileSystem :: get_error_string(fres));
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

// called from GUI task
void IecInterface :: start_warp(int drive)
{
	// First try to obtain a lock on this function
	if (xSemaphoreTake(ulticopyMutex, 0) == pdFALSE) {
		cmd_ui->popup("Ulticopy is already in use", BUTTON_OK);
		return;
	}

	// FIXME: Make sure that the C64 is actually frozen!
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

    warp_drive = drive;
    int command = 1;
    xQueueSend(queueGuiToIec, &command, 0); // ulticopy shall now take over

    if (xSemaphoreTake(ulticopyBusy, 10) == pdFALSE) {
    	printf("Synchronization error!  UltiCopy did not start.\n");
    	while(1)
    		;
    }
    while(1) {
		printf("GUI Task was notified that UltiCopy is now busy");
		xSemaphoreTake(ulticopyBusy, portMAX_DELAY);

		// Ulticopy is done
		printf("UltiCopy warp mode returned.\n");

		if(warp_return_code & 0x80) {
			last_track++;
			printf("Error on track %d.\n", last_track);
		} else if(warp_return_code == 0) {
			save_copied_disk();
			if(cmd_ui->popup("Another disk?", BUTTON_YES|BUTTON_NO) == BUTTON_YES) {
				ui_window->window->clear();
				// if(!run_drive_code(warp_drive, 0x403, NULL, 0)) { // restart
			    if(!run_drive_code(warp_drive, 0x400, &_ulticopy_65_start, (int)&_ulticopy_65_size)) {
					cmd_ui->popup("Restart error..", BUTTON_OK);
					break;
			    }
				// clear pending interrupt if any
				HW_IEC_IRQ = 0;
				// push warp command into down-fifo
				HW_IEC_TX_FIFO_RELEASE = 1;
				HW_IEC_TX_CTRL = IEC_CMD_GO_WARP;
				last_track = 0;
			} else {
				ui_window->close();
				break;
			}
		} else if(warp_return_code < 0x40) {
			cmd_ui->popup("Error reading disk..", BUTTON_OK);
			ui_window->close();
			break;
		}
	}
	if (c1541_A) {
		c1541_A->effectuate_settings();
	}
	if (c1541_B) {
		c1541_B->effectuate_settings();
	}
	HW_IEC_RESET_ENABLE = iec_enable;

    xSemaphoreGive(ulticopyMutex);
}

void IecInterface :: start_warp_iec(void)
{
	// notify that we are busy now
	xSemaphoreGive(ulticopyBusy);

    HW_IEC_RESET_ENABLE = 1;
    
    if(!run_drive_code(warp_drive, 0x400, &_ulticopy_65_start, (int)&_ulticopy_65_size)) {
        cmd_ui->popup("Error accessing drive..", BUTTON_OK);
        ui_window->close();
        warp_return_code = 0x1F;
        xSemaphoreGive(ulticopyBusy);
        return;
    }

    ui_window->window->clear();
    // clear pending interrupt if any
    HW_IEC_IRQ = 0;
    // push warp command into down-fifo; Go!
	HW_IEC_TX_FIFO_RELEASE = 1;
    HW_IEC_TX_CTRL = IEC_CMD_GO_WARP;
    last_track = 0;
}

void IecInterface :: get_warp_data(void)
{
    uint32_t read;
    uint32_t temp[65];
    uint32_t *dw = &temp[0];

    int err = 0;
    uint16_t fifo_count = uint16_t(HW_IEC_UP_FIFO_COUNT_LO) | (uint16_t(HW_IEC_UP_FIFO_COUNT_HI) << 8);

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

#if NIOS
    uint8_t sector = (uint8_t)(read >> 24);
#else
    uint8_t sector = (uint8_t)read;
#endif
    uint8_t track = HW_IEC_RX_DATA;
    uint8_t *dest = static_bin_image.get_sector_pointer(track, sector);
    uint8_t *src = (uint8_t *)temp;
    // printf("Sector {%b %b (%p -> %p}\n", track, sector, src, dest);
    if (dest) {
		ui_window->window->set_char(track-1,sector+1,err?'-':'*');
		ui_window->parent_win->sync();
		last_track = track;
		if(dest) {
			for(int i=0;i<256;i++) {
				*(dest++) = *(++src); // asymmetric: We copy from 1.. to 0..., so we increment src first
			}
		}
    }
    // clear pending interrupt
    wait_irq = false;
    HW_IEC_IRQ = 0;
}

// called from IEC context
void IecInterface :: get_warp_error(void)
{
    while (HW_IEC_RX_FIFO_STATUS & IEC_FIFO_EMPTY)
        ;
        
    warp_return_code = HW_IEC_RX_DATA;
    printf("{Warp Error: %b}", warp_return_code);
    // clear pending interrupt
    HW_IEC_IRQ = 0;
    // notify the gui that we are done
    xSemaphoreGive(ulticopyBusy);
}

void IecInterface :: save_copied_disk()
{
    char buffer[40];
    int save_result;
    File *f = 0;
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
        FRESULT fres = fm->fopen(cmd_path, buffer, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &f);
		if(f) {
            cmd_ui->show_progress("Saving D64..", 35);
            save_result = bin->save(f, cmd_ui);
            cmd_ui->hide_progress();
    		printf("Result of save: %d.\n", save_result);
            fm->fclose(f);
		} else {
			printf("Can't create file '%s': %s\n", buffer, FileSystem::get_error_string(fres));
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
	HW_IEC_TX_FIFO_RELEASE = 1;
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
    
	HW_IEC_TX_FIFO_RELEASE = 1;
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
	HW_IEC_TX_FIFO_RELEASE = 1;
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
		HW_IEC_TX_FIFO_RELEASE = 1;
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
		HW_IEC_TX_FIFO_RELEASE = 1;
        HW_IEC_TX_CTRL = IEC_CMD_GO_MASTER;
        HW_IEC_TX_DATA = 0x48; // talk #8!!
        HW_IEC_TX_DATA = 0x60; // channel 0
        HW_IEC_TX_CTRL = IEC_CMD_ATN_TO_RX;
        break;
    case 3:
        printf("Close file on 8\n");
		HW_IEC_TX_FIFO_RELEASE = 1;
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
		HW_IEC_TX_FIFO_RELEASE = 1;
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

FRESULT IecInterface :: readDirectory()
{
	cleanupDir();
	FRESULT res = path->get_directory(*dirlist);
	for(int i=0;i<dirlist->get_elements();i++) {
		iecNames->append(getIecName((*dirlist)[i]->lfname));
	}
	return res;
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
		if (pattern_match(name, (*iecNames)[i], false)) {
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

int UltiCopy :: poll(int a)
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

