#include "integer.h"
extern "C" {
	#include "dump_hex.h"
    #include "small_printf.h"
}
#include "iec.h"
#include "poll.h"
#include "c64.h"
#include "filemanager.h"

// this global will cause us to run!
IecInterface iec_if;

void poll_iec_interface(Event &ev)
{
    iec_if.poll(ev);
}

IecInterface :: IecInterface()
{
    if(CAPABILITIES & CAPAB_HARDWARE_IEC) {
        poll_list.append(&poll_iec_interface);
    
        printf("IEC Processor found: Version = %b\n", HW_IEC_VERSION);
    
    }
    atn = false;
}

IecInterface :: ~IecInterface()
{
	poll_list.remove(&poll_iec_interface);
}

int IecInterface :: poll(Event &e)
{
    int a;
    BYTE data;
    while (!((a = HW_IEC_RX_FIFO_STATUS) & IEC_FIFO_EMPTY)) {
        data = HW_IEC_RX_DATA;
        if(a & IEC_FIFO_CTRL) {
            switch(data) {
                case 0x01:
                    atn = true;
                    break;
                case 0x02:
                    atn = false;
                    break;
                case 0x03:
                    for(int i=0;i<10;i++)
                        HW_IEC_TX_DATA = 0x40 + (BYTE)i;
                    HW_IEC_TX_LAST = 0xAA;
                    break;
                case 0x0E:
                    printf("{end} ");
                    break;
                default:
                    printf("<%b> ", data);
            }
        } else {
            if(atn)
                printf("[/%b] ", data);
            else
                printf("[%b] ", data);
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
