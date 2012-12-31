#ifndef DOS_H
#define DOS_H

#include "integer.h"
#include "event.h"
#include "filemanager.h"
#include "indexed_list.h"
#include "config.h"
#include "command_intf.h"

#define DOS_FILES 5

#define DOS_CMD_IDENTIFY   0x01
#define DOS_CMD_OPEN_FILE  0x02
#define DOS_CMD_CLOSE_FILE 0x03
#define DOS_CMD_CHANGE_DIR 0x04
#define DOS_CMD_GET_PATH   0x05
#define DOS_CMD_OPEN_DIR   0x06
#define DOS_CMD_READ_DIR   0x07
#define DOS_CMD_CLOSE_DIR  0x08
#define DOS_CMD_READ_DATA  0x09
#define DOS_CMD_WRITE_DATA 0x0A

class Dos : CommandTarget
{
    
public:
    Dos(int id);
    ~Dos();

    void parseCommand(Message *command, Message **reply, Message **status);
    bool getMoreData(Message **reply, Message **status);  
};

#endif

