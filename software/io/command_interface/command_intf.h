#ifndef COMMAND_INTF_H
#define COMMAND_INTF_H

#include "integer.h"
#include "event.h"
#include "menu.h"

#define CMD_IF_BASE      0x4044000
#define CMD_IF_RAM_BASE  0x4044800

#define CMD_IF_RAM(x)          *((volatile BYTE *)(CMD_IF_RAM_BASE +x))

#define CMD_IF_SLOT_BASE       *((volatile BYTE *)(CMD_IF_BASE + 0x0))
#define CMD_IF_SLOT_ENABLE     *((volatile BYTE *)(CMD_IF_BASE + 0x1))
#define CMD_IF_HANDSHAKE_OUT   *((volatile BYTE *)(CMD_IF_BASE + 0x2))
#define CMD_IF_STATUSBYTE      *((volatile BYTE *)(CMD_IF_BASE + 0x3))
#define CMD_IF_COMMAND_START   *((volatile BYTE *)(CMD_IF_BASE + 0x4))
#define CMD_IF_COMMAND_END     *((volatile BYTE *)(CMD_IF_BASE + 0x5))
#define CMD_IF_RESPONSE_START  *((volatile BYTE *)(CMD_IF_BASE + 0x6))
#define CMD_IF_RESPONSE_END    *((volatile BYTE *)(CMD_IF_BASE + 0x7))
#define CMD_IF_STATUS_START    *((volatile BYTE *)(CMD_IF_BASE + 0x8))
#define CMD_IF_STATUS_END      *((volatile BYTE *)(CMD_IF_BASE + 0x9))
#define CMD_IF_STATUS_LENGTH   *((volatile BYTE *)(CMD_IF_BASE + 0xA))
#define CMD_IF_RESPONSE_LEN_L  *((volatile BYTE *)(CMD_IF_BASE + 0xC))
#define CMD_IF_RESPONSE_LEN_H  *((volatile BYTE *)(CMD_IF_BASE + 0xD))
#define CMD_IF_COMMAND_LEN_L   *((volatile BYTE *)(CMD_IF_BASE + 0xE))
#define CMD_IF_COMMAND_LEN_H   *((volatile BYTE *)(CMD_IF_BASE + 0xF))

#define CMD_NEW_COMMAND           0x01
#define CMD_DATA_ACCEPTED         0x02
#define CMD_ABORT_DATA            0x04
#define CMD_MORE_OR_ABORT         0x06
#define CMD_STATE_BITS            0x30
#define CMD_STATE_IDLE            0x00
#define CMD_STATE_PROCESSING      0x10
#define CMD_STATE_DATA_LAST       0x20
#define CMD_STATE_DATA_MORE       0x30

#define HANDSHAKE_RESET           0xff
#define HANDSHAKE_ACCEPT_COMMAND  0x01
#define HANDSHAKE_ACCEPT_NEXTDATA 0x02
#define HANDSHAKE_ACCEPT_ABORT    0x04
#define HANDSHAKE_VALIDATE_LAST   0x10
#define HANDSHAKE_VALIDATE_MORE   0x30

typedef struct _message
{
    int   length;
    BYTE *message;
} Message;

class CommandInterface : public ObjectWithMenu
{
    BYTE *command_buffer;
    BYTE *response_buffer;
    BYTE *status_buffer;

    Message incoming_command;
    
public:
    CommandInterface();
    ~CommandInterface();
    
    int poll(Event &ev);
    void dump_registers(void);
    int  fetch_task_items(IndexedList<PathObject*> &item_list);
};

extern CommandInterface cmd_if;
 
void poll_command_interface(Event &ev);

/* Command Targets */

#define CMD_IF_MAX_TARGET   0x0F

// Mandatory commands for all targets
#define CMD_IDENTIFY   0x01

extern Message c_message_no_target;
extern Message c_status_ok;
extern Message c_status_unknown_command;
extern Message c_message_empty;

class CommandTarget
{

public:
    CommandTarget() { }
    virtual ~CommandTarget() { }

    virtual void parse_command(Message *command, Message **reply, Message **status) {
        if(command->message[1] == CMD_IDENTIFY) {
            *reply  = &c_message_no_target;
            *status = &c_status_ok;
        } else {
            *reply  = &c_message_empty;
            *status = &c_status_unknown_command; 
        }        
    }
    virtual bool get_more_data(Message **reply, Message **status) {
        *reply  = &c_message_empty;
        *status = &c_status_ok; 
        return false;
    }
};

extern CommandTarget *command_targets[];


#endif
