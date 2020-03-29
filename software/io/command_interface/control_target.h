#ifndef CONTROL_TARGET_H
#define CONTROL_TARGET_H

#include "integer.h"
#include "command_intf.h"
#include "c1541.h"

#define CTRL_CMD_IDENTIFY       0x01
#define CTRL_CMD_READ_RTC		0x02
#define CTRL_CMD_FINISH_CAPTURE 0x03
#define CTRL_CMD_FREEZE         0x05
#define CTRL_CMD_REBOOT         0x06
#define CTRL_CMD_LOAD_REU       0x08
#define CTRL_CMD_SAVE_REU       0x09
#define CTRL_CMD_U64_SAVEMEM    0x0F
#define CTRL_CMD_DECODE_TRACK   0x11
#define CTRL_CMD_ENCODE_TRACK   0x12
#define CTRL_CMD_EASYFLASH      0x20
#define CTRL_CMD_GET_HWINFO     0x28
#define CTRL_CMD_ENABLE_DISK_A  0x30
#define CTRL_CMD_DISABLE_DISK_A 0x31
#define CTRL_CMD_ENABLE_DISK_B  0x32
#define CTRL_CMD_DISABLE_DISK_B 0x33
#define CTRL_CMD_DISK_A_POWER   0x34
#define CTRL_CMD_DISK_B_POWER   0x35

class ControlTarget : CommandTarget
{
    Message data_message;
    Message status_message;
    void decode_track(Message *command, Message **reply, Message **status);
    void save_u64_memory(Message *command);
public:
    ControlTarget(int id);
    ~ControlTarget();

    void parse_command(Message *command, Message **reply, Message **status);
    void get_more_data(Message **reply, Message **status);  
    void abort(void);
};

#endif
