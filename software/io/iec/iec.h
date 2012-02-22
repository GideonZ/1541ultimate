#ifndef IEC_H
#define IEC_H

#include "integer.h"
#include "event.h"
#include "menu.h"

#define HW_IEC_REGS      0x4028000
#define HW_IEC_CODE      0x4028800
#define HW_IEC_CODE_BE   0x5028800

#define HW_IEC_RAM(x)          *((volatile BYTE *)(HW_IEC_CODE +x))

#define HW_IEC_VERSION         *((volatile BYTE *)(HW_IEC_REGS + 0x0))
#define HW_IEC_TX_FIFO_STATUS  *((volatile BYTE *)(HW_IEC_REGS + 0x1)) // 1: full 0: empty
#define HW_IEC_RX_FIFO_STATUS  *((volatile BYTE *)(HW_IEC_REGS + 0x2)) // 7: ctrlcode 1: full 0: empty
#define HW_IEC_RESET           *((volatile BYTE *)(HW_IEC_REGS + 0x3)) 
#define HW_IEC_TX_DATA         *((volatile BYTE *)(HW_IEC_REGS + 0x4)) // push
#define HW_IEC_TX_LAST         *((volatile BYTE *)(HW_IEC_REGS + 0x5)) // push with EOI
#define HW_IEC_RX_DATA         *((volatile BYTE *)(HW_IEC_REGS + 0x6)) // read+clear
#define HW_IEC_RX_CTRL         *((volatile BYTE *)(HW_IEC_REGS + 0x7)) // read

#define IEC_FIFO_EMPTY 0x01
#define IEC_FIFO_FULL  0x02
#define IEC_FIFO_CTRL  0x80

#define LOGGER_BASE            0x4060300
#define LOGGER_ADDRESS         *((volatile DWORD *)(LOGGER_BASE + 0x0)) // read
#define LOGGER_LENGTH          *((volatile BYTE  *)(LOGGER_BASE + 0x4)) // read
#define LOGGER_COMMAND         *((volatile BYTE  *)(LOGGER_BASE + 0x5)) // write

#define LOGGER_CMD_START       0x33
#define LOGGER_CMD_STOP        0x44

class IecChannel;

class IecInterface : public ObjectWithMenu
{
    bool atn;
    bool talking;
    DWORD start_address;
    DWORD end_address;
    IecChannel *channels[16];
    int current_channel;
public:
    Path *path;

    IecInterface();
    ~IecInterface();
    
    int poll(Event &ev);
    int fetch_task_items(IndexedList<PathObject *> &list);

};

extern IecInterface HW_IEC;
 
void poll_iec_interface(Event &ev);

// FileType to load code
#include "file_direntry.h"

class FileTypeIEC : public FileDirEntry
{
public:
    FileTypeIEC(FileTypeFactory &fac);
    FileTypeIEC(PathObject *par, FileInfo *fi);
    ~FileTypeIEC();

    int   fetch_context_items(IndexedList<PathObject *> &list);
    FileDirEntry *test_type(PathObject *obj);
    void execute(int selection);
};


#endif
