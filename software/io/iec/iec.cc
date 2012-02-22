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

// this global will cause us to run!
IecInterface iec_if;

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
    printf("IEC Processor found: Version = %b\n", HW_IEC_VERSION);
    atn = false;
    path = new Path; // starts in SdCard root ;)

    for(int i=0;i<16;i++) {
        channels[i] = new IecChannel(this, i);
    }
    current_channel = 0;
    talking = false;
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

int IecInterface :: fetch_task_items(IndexedList<PathObject *> &list)
{
	list.append(new ObjectMenuItem(this, "Reset IEC",      MENU_IEC_RESET));
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
                    printf("<%b> ", data);
                    break;
                case 0x42:
                    atn = false;
                    printf("<%b> ", data);
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
                HW_IEC_RESET = 1;
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
                dump_hex((void *)HW_IEC_CODE, 128);
                HW_IEC_RESET = 1;
                file->read((void *)HW_IEC_CODE_BE, 2048, &bytes_read);
                printf("Read %d code bytes.\n", bytes_read);
                HW_IEC_RESET = 1;
                dump_hex((void *)HW_IEC_CODE, 128);
            }
            break;
        default:
            break;
    }
}
