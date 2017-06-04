#ifndef CONTROL_TARGET_H
#define CONTROL_TARGET_H

#include "integer.h"
#include "command_intf.h"

#define CTRL_CMD_IDENTIFY       0x01
#define CTRL_CMD_READ_RTC		0x02
#define CTRL_CMD_DECODE_TRACK   0x11
#define CTRL_CMD_ENCODE_TRACK   0x12


class ControlTarget : CommandTarget
{
    Message data_message;
    Message status_message;
    void decode_track(Message *command, Message **reply, Message **status);
public:
    ControlTarget(int id);
    ~ControlTarget();

    void parse_command(Message *command, Message **reply, Message **status);
    void get_more_data(Message **reply, Message **status);  
    void abort(void);
};

#endif
