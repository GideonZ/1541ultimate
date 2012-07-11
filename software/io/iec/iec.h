#ifndef IEC_H
#define IEC_H

#include "integer.h"
#include "event.h"
#include "menu.h"
#include "config.h"

#define HW_IEC_REGS      0x4028000
#define HW_IEC_CODE      0x4028800
#define HW_IEC_CODE_BE   0x5028800

#define HW_IEC_RAM(x)          *((volatile BYTE *)(HW_IEC_CODE +x))
#define HW_IEC_RAM_DW           ((volatile DWORD *)(HW_IEC_CODE))

#define HW_IEC_VERSION         *((volatile BYTE *)(HW_IEC_REGS + 0x0))
#define HW_IEC_TX_FIFO_STATUS  *((volatile BYTE *)(HW_IEC_REGS + 0x1)) // 1: full 0: empty
#define HW_IEC_RX_FIFO_STATUS  *((volatile BYTE *)(HW_IEC_REGS + 0x2)) // 7: ctrlcode 1: full 0: empty
#define HW_IEC_RESET_ENABLE    *((volatile BYTE *)(HW_IEC_REGS + 0x3)) // 0 = reset, 1 = run (and reset)
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

class IecInterface : public ObjectWithMenu,  ConfigurableObject
{
    int last_addr;
    bool atn;
    bool talking;
    DWORD start_address;
    DWORD end_address;
    IecChannel *channels[16];
    int current_channel;
public:
    int last_error;
    Path *path;
    BYTE iec_enable;

    IecInterface();
    ~IecInterface();
    
    int poll(Event &ev);
    int fetch_task_items(IndexedList<PathObject *> &list);
    void effectuate_settings(void); // from ConfigurableObject
    int get_last_error(char *); // writes string into buffer
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

#define ERR_OK							00
#define ERR_FILES_SCRATCHED				01
#define ERR_READ_ERROR					20
#define ERR_WRITE_ERROR					25
#define ERR_WRITE_PROTECT_ON			26
#define ERR_DISK_ID_MISMATCH			29
#define ERR_SYNTAX_ERROR_GEN			30
#define ERR_SYNTAX_ERROR_CMD			31
#define ERR_SYNTAX_ERROR_CMDLENGTH		32
#define ERR_SYNTAX_ERROR_NAME			33
#define ERR_SYNTAX_ERROR_NONAME			34
#define ERR_SYNTAX_ERROR_CMD15			39
#define ERR_RECORD_NOT_PRESENT          50
#define ERR_OVERFLOW_IN_RECORD          51
#define ERR_WRITE_FILE_OPEN				60
#define ERR_FILE_NOT_OPEN				61
#define ERR_FILE_NOT_FOUND				62
#define ERR_FILE_EXISTS					63
#define ERR_FILE_TYPE_MISMATCH			64
#define ERR_NO_CHANNEL          		70
#define ERR_DIRECTORY_ERROR				71
#define ERR_DISK_FULL					72
#define ERR_DOS							73
#define ERR_DRIVE_NOT_READY				74
// custom
#define ERR_BAD_COMMAND					75
#define ERR_UNIMPLEMENTED				76

typedef struct {
	BYTE nr;
	CHAR* msg;
	BYTE len;
} IEC_ERROR_MSG;


#endif
