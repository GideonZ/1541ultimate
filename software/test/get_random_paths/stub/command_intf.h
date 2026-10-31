// Stand-in for command_intf.h: what network_target.h declares against
#ifndef COMMAND_INTF_H_STUB
#define COMMAND_INTF_H_STUB
#include <stdint.h>
#define CMD_MAX_REPLY_BLOCK 895
typedef struct _message {
    int   length;
    bool  last_part;
    uint8_t *message;
} Message;
class CommandTarget {
public:
    virtual ~CommandTarget() {}
    virtual void parse_command(Message *command, Message **reply, Message **status) = 0;
    virtual void get_more_data(Message **reply, Message **status) {}
    virtual void abort(int a) {}
    virtual void c64_reset(void) {}
};
#endif
