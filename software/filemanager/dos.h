#ifndef DOS_H
#define DOS_H

#include "integer.h"
#include "filemanager.h"
#include "indexed_list.h"
#include "config.h"
#include "command_intf.h"
#include "c1541.h"

#define DOS_CMD_IDENTIFY       0x01
#define DOS_CMD_OPEN_FILE      0x02
#define DOS_CMD_CLOSE_FILE     0x03
#define DOS_CMD_READ_DATA      0x04
#define DOS_CMD_WRITE_DATA     0x05
#define DOS_CMD_FILE_SEEK      0x06
#define DOS_CMD_FILE_INFO      0x07
#define DOS_CMD_FILE_STAT      0x08
#define DOS_CMD_DELETE_FILE    0x09
#define DOS_CMD_RENAME_FILE    0x0a
#define DOS_CMD_COPY_FILE      0x0b
#define DOS_CMD_CHANGE_DIR     0x11
#define DOS_CMD_GET_PATH       0x12
#define DOS_CMD_OPEN_DIR       0x13
#define DOS_CMD_READ_DIR       0x14
#define DOS_CMD_COPY_UI_PATH   0x15
#define DOS_CMD_CREATE_DIR     0x16
#define DOS_CMD_COPY_HOME_PATH 0x17
#define DOS_CMD_LOAD_REU       0x21
#define DOS_CMD_SAVE_REU       0x22
#define DOS_CMD_MOUNT_DISK     0x23
#define DOS_CMD_UMOUNT_DISK    0x24
#define DOS_CMD_SWAP_DISK      0x25
#define DOS_CMD_GET_TIME       0x26
#define DOS_CMD_SET_TIME       0x27
#define DOS_CMD_ECHO           0xF0

typedef enum _e_dos_state {
    e_dos_idle,
    e_dos_in_file,
    e_dos_in_directory
} e_dos_state;

typedef struct _dos_info {
    uint32_t size;  /* File size */
    uint16_t date;  /* Last modified date */
    uint16_t time;  /* Last modified time */
    char    extension[3];
    uint8_t attrib;  /* Attribute */
    char    filename[64];
} t_dos_info;

class Dos : CommandTarget
{
    FileManager *fm;
    e_dos_state dos_state;
    t_dos_info dos_info;
    File *file;
    Path *path;
    IndexedList<FileInfo *>directoryList;
    Message data_message;
    Message status_message;
    int remaining;
    int dir_entries;
    int current_index;
    void cleanupDirectory();
    void cd(Message *command, Message **reply, Message **status);
    C1541* getDriveByID(uint8_t id);
public:
    Dos(int id);
    ~Dos();

    void parse_command(Message *command, Message **reply, Message **status);
    void get_more_data(Message **reply, Message **status);  
    void abort(void);
};

#endif

