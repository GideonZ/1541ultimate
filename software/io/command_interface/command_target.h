#ifndef _COMMAND_TARGET_H
#define _COMMAND_TARGET_H

#include <stdint.h>
#include "operation_cancellation.h"

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

    // Called by the owner before dispatch. The source outlives all targets;
    // interrupt handlers only signal it, never call a target or free a session.
    virtual void begin_command(const OperationCancellation&) { }
    // Publication check runs with interrupts masked: no allocation, blocking,
    // I/O or cleanup. finish_reply runs afterwards in the owning task.
    virtual void prepare_reply(Message **, Message **) { }
    virtual void finish_reply(bool) { }

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

    // The C64 was reset, so the program talking to this target is gone.
    virtual void c64_reset(void) { }

    // A command that this target answers at once, with no lock, file, socket or long
    // loop: the UCI Server task runs it at its own level instead of lowering itself, so it
    // never waits behind the services. Everything else runs at PRIO_HW_SERVICE.
    virtual bool is_quick(Message *command) { return false; }

    // The reply to a command that this target deferred; see command_defer(). Called on the
    // UCI Server task, with no other command in between. Fills reply and status as
    // parse_command does; get_more_data hands out the rest, as after parse_command.
    virtual void deferred_reply(Message **reply, Message **status) {
        *reply  = &c_message_empty;
        *status = &c_status_ok;
    }
};

extern CommandTarget *command_targets[];

// A command whose reply has to wait for something other than the C64, such as a network
// peer, must not keep the UCI Server task waiting: abort and C64 reset are handled there.
// From parse_command, the target calls command_defer(), hands the work to its own task and
// returns. The C64 sees the command BUSY, exactly as while a target works on it. When the
// work is done, that task calls command_deferred_done() with the ticket it was given, and
// the UCI Server task calls deferred_reply() to send the reply. An abort or a C64 reset
// while the command waits ends it at once: the target's abort() or c64_reset() is called,
// a late result is never sent, and the target frees it.
uint32_t command_defer(void);
void command_deferred_done(uint32_t ticket);

#ifndef CMD_MAX_REPLY_LEN
#define CMD_MAX_COMMAND_LEN 896
#define CMD_MAX_REPLY_LEN   896
#define CMD_MAX_STATUS_LEN  256
#endif

// The FPGA response pointer stops at the last buffer byte while DATA_AV stays
// set, so a reply block must leave one byte unused. See command_protocol.vhd.
#define CMD_MAX_REPLY_BLOCK (CMD_MAX_REPLY_LEN - 1)

#endif
