#ifndef DOS_H
#define DOS_H

#include "integer.h"
#include "event.h"
#include "filemanager.h"
#include "indexed_list.h"
#include "config.h"
#include "command_intf.h"

#define DOS_CMD_IDENTIFY     0x01
#define DOS_CMD_OPEN_FILE    0x02
#define DOS_CMD_CLOSE_FILE   0x03
#define DOS_CMD_READ_DATA    0x04
#define DOS_CMD_WRITE_DATA   0x05
#define DOS_CMD_FILE_SEEK    0x06
#define DOS_CMD_FILE_INFO    0x07
#define DOS_CMD_CHANGE_DIR   0x11
#define DOS_CMD_GET_PATH     0x12
#define DOS_CMD_OPEN_DIR     0x13
#define DOS_CMD_READ_DIR     0x14
#define DOS_CMD_COPY_UI_PATH 0x15
#define DOS_CMD_LOAD_REU     0x21
#define DOS_CMD_SAVE_REU     0x22
#define DOS_CMD_ECHO         0xF0

typedef enum _e_dos_state {
    e_dos_idle,
    e_dos_in_file,
    e_dos_in_directory
} e_dos_state;

typedef struct _dos_info {
    uint32_t	size;	 /* File size */
	WORD	date;	 /* Last modified date */
	WORD	time;	 /* Last modified time */
    char    extension[3];
	uint8_t	attrib;	 /* Attribute */
    char    filename[64];
} t_dos_info;

class Dos : CommandTarget
{
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

public:
    Dos(int id);
    ~Dos();

    void parse_command(Message *command, Message **reply, Message **status);
    void get_more_data(Message **reply, Message **status);  
    void abort(void);
};

#endif

