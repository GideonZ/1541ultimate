#ifndef COMMAND_INTF_H
#define COMMAND_INTF_H

#include "integer.h"
#include "menu.h"
#include "config.h"
#include "iomap.h"
#include "subsys.h"

#define CMD_IF_RAM_BASE  (CMD_IF_BASE + 0x800)

#define CMD_IF_RAM(x)          *((volatile uint8_t *)(CMD_IF_RAM_BASE +x))

#define CMD_IF_SLOT_BASE       *((volatile uint8_t *)(CMD_IF_BASE + 0x0))
#define CMD_IF_SLOT_ENABLE     *((volatile uint8_t *)(CMD_IF_BASE + 0x1))
#define CMD_IF_HANDSHAKE_OUT   *((volatile uint8_t *)(CMD_IF_BASE + 0x2))
#define CMD_IF_STATUSBYTE      *((volatile uint8_t *)(CMD_IF_BASE + 0x3))
#define CMD_IF_COMMAND_START   *((volatile uint8_t *)(CMD_IF_BASE + 0x4))
#define CMD_IF_COMMAND_END     *((volatile uint8_t *)(CMD_IF_BASE + 0x5))
#define CMD_IF_RESPONSE_START  *((volatile uint8_t *)(CMD_IF_BASE + 0x6))
#define CMD_IF_RESPONSE_END    *((volatile uint8_t *)(CMD_IF_BASE + 0x7))
#define CMD_IF_STATUS_START    *((volatile uint8_t *)(CMD_IF_BASE + 0x8))
#define CMD_IF_STATUS_END      *((volatile uint8_t *)(CMD_IF_BASE + 0x9))
#define CMD_IF_STATUS_LENGTH   *((volatile uint8_t *)(CMD_IF_BASE + 0xA))
#define CMD_IF_RESPONSE_LEN_L  *((volatile uint8_t *)(CMD_IF_BASE + 0xC))
#define CMD_IF_RESPONSE_LEN_H  *((volatile uint8_t *)(CMD_IF_BASE + 0xD))
#define CMD_IF_COMMAND_LEN_L   *((volatile uint8_t *)(CMD_IF_BASE + 0xE))
#define CMD_IF_COMMAND_LEN_H   *((volatile uint8_t *)(CMD_IF_BASE + 0xF))

#define CMD_NEW_COMMAND           0x01
#define CMD_DATA_ACCEPTED         0x02
#define CMD_ABORT_DATA            0x04
#define CMD_MORE_OR_ABORT         0x06
#define CMD_STATE_BITS            0x30
#define CMD_STATE_IDLE            0x00
#define CMD_STATE_PROCESSING      0x10
#define CMD_STATE_DATA_LAST       0x20
#define CMD_STATE_DATA_MORE       0x30

#define HANDSHAKE_RESET           0x87
#define HANDSHAKE_ACCEPT_COMMAND  0x01
#define HANDSHAKE_ACCEPT_NEXTDATA 0x02
#define HANDSHAKE_ACCEPT_ABORT    0x04
#define HANDSHAKE_VALIDATE_LAST   0x10
#define HANDSHAKE_VALIDATE_MORE   0x30

#define CMD_TARGET_NONE 0xFF

typedef struct _message
{
    int   length;
    bool  last_part;
    uint8_t *message;
} Message;

class CommandInterface : public SubSystem, ObjectWithMenu
{
    uint8_t *command_buffer;
    uint8_t *response_buffer;
    uint8_t *status_buffer;

    Message incoming_command;
    uint8_t target;    
    void copy_result(Message *data, Message *status);
    int cart_mode;
    
    int executeCommand(SubsysCommand *cmd);
public:
    CommandInterface();
    ~CommandInterface();
    
    int poll(void);
    void dump_registers(void);
    int  fetch_task_items(Path *path, IndexedList<Action*> &item_list);
    const char *identify(void) { return "Command Interface"; }
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
    virtual void get_more_data(Message **reply, Message **status) {
        *reply  = &c_message_empty;
        *status = &c_status_ok; 
    }
    
    virtual void abort(void) { }
};

extern CommandTarget *command_targets[];


#endif
