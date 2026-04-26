#ifndef COMMAND_INTF_H
#define COMMAND_INTF_H

#include <stdint.h>

typedef struct _message
{
    int   length;
    bool  last_part;
    uint8_t *message;
} Message;

class CommandInterface
{
public:
	CommandInterface() {}
    ~CommandInterface() {} 
    
    void set_kernal_device_id(uint8_t id) {} 
};

extern CommandInterface cmd_if;

#endif // COMMAND_INTF_H
