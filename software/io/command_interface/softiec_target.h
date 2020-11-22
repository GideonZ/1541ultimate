#ifndef CONTROL_TARGET_H
#define CONTROL_TARGET_H

#include <stdint.h>
#include "command_intf.h"
#include "iec.h"
#include "iec_channel.h"

#define SOFTIEC_CMD_IDENTIFY       0x01
#define SOFTIEC_CMD_LOAD_SU        0x10
#define SOFTIEC_CMD_LOAD_EX        0x11
#define SOFTIEC_CMD_SAVE           0x12
#define SOFTIEC_CMD_OPEN           0x13
#define SOFTIEC_CMD_CLOSE          0x14
#define SOFTIEC_CMD_CHKIN          0x15
#define SOFTIEC_CMD_CHKOUT         0x16

class SoftIECTarget : CommandTarget
{
    Message data_message;
    Message status_message;
    uint16_t startaddr;
    IecChannel *input_channel;
    int input_length;

    void cmd_load_su(Message *command, Message **reply, Message **status);
    void cmd_load_ex(Message *command, Message **reply, Message **status);
    void cmd_save(Message *command, Message **reply, Message **status);
    void cmd_open(Message *command, Message **reply, Message **status);
    void cmd_close(Message *command, Message **reply, Message **status);
    void cmd_chkin(Message *command, Message **reply, Message **status);
    void cmd_chkout(Message *command, Message **reply, Message **status);
    uint16_t get_start_addr(IecChannel *chan);
    uint16_t do_load(IecChannel *chan, uint16_t startaddr);
    bool do_verify(IecChannel *chan, uint16_t startaddr);
    bool do_save(IecChannel *chan, uint16_t start, uint16_t end);
    void prepare_data(int count);
public:
    SoftIECTarget(int id);
    ~SoftIECTarget();

    void parse_command(Message *command, Message **reply, Message **status);
    void get_more_data(Message **reply, Message **status);  
    void abort(int);
};

#endif
