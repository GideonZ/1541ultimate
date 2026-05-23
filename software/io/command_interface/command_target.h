#ifndef _COMMAND_TARGET_H
#define _COMMAND_TARGET_H

#include <stdint.h>

/* Command Targets */

#define CMD_IF_MAX_TARGET   0x0F
#define CMD_IF_NO_REPLY     0x80

// Mandatory commands for all targets
#define CMD_IDENTIFY   0x01

typedef struct _message
{
    int   length;
    bool  last_part;
    uint8_t *message;
} Message;

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
    
    virtual void abort(int a) { }
};

extern CommandTarget *command_targets[];

#ifndef CMD_MAX_REPLY_LEN
#define CMD_MAX_COMMAND_LEN 896
#define CMD_MAX_REPLY_LEN   896
#define CMD_MAX_STATUS_LEN  256
#endif

#endif