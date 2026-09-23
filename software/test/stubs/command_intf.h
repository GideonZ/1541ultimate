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
	CommandInterface() : kernal_device_id(0) {}
    ~CommandInterface() {}

    // What the firmware would put at $DF1B for the KERNAL to compare with.
    uint8_t kernal_device_id;
    void set_kernal_device_id(uint8_t id) { kernal_device_id = id; }
};

extern CommandInterface cmd_if;

#endif // COMMAND_INTF_H
