#ifndef UCI_COMMANDS_H
#define UCI_COMMANDS_H

#include "integer.h"
#include "config.h"
#include "command_intf.h"

#define UCI_CMD_IDENTIFY       0x01
#define UCI_CMD_ENTER_MENU     0x02
#define UCI_CMD_FINISH_CAPTURE 0x03
#define UCI_CMD_READ_RTC       0x04

class UCI_Commands : CommandTarget
{
    Message data_message;
    Message status_message;
public:
    UCI_Commands(int id);
    ~UCI_Commands();

    void parse_command(Message *command, Message **reply, Message **status);
    void get_more_data(Message **reply, Message **status);  
    void abort(void);
};

#endif

