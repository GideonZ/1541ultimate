#ifndef CONTROL_TARGET_H
#define CONTROL_TARGET_H

#include "integer.h"
#include "command_intf.h"
#include "iec.h"
#include "iec_channel.h"

#define SOFTIEC_CMD_IDENTIFY       0x01
#define SOFTIEC_CMD_LOAD           0x11
#define SOFTIEC_CMD_SAVE           0x12

class SoftIECTarget : CommandTarget
{
    Message data_message;
    Message status_message;

    void cmd_load(Message *command, Message **reply, Message **status);
    void cmd_save(Message *command, Message **reply, Message **status);
    uint16_t get_start_addr(IecChannel *chan);
    uint16_t do_load(IecChannel *chan, uint16_t startaddr);
    bool do_verify(IecChannel *chan, uint16_t startaddr);
    bool do_save(IecChannel *chan, uint16_t start, uint16_t end);

public:
    SoftIECTarget(int id);
    ~SoftIECTarget();

    void parse_command(Message *command, Message **reply, Message **status);
    void get_more_data(Message **reply, Message **status);  
    void abort(void);
};

#endif
