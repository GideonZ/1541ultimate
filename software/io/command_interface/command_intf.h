#ifndef COMMAND_INTF_H
#define COMMAND_INTF_H

#include "integer.h"
#include "event.h"

#define CMD_IF_BASE      0x4044000
#define CMD_IF_RAM_BASE  0x4044800

#define CMD_IF_RAM(x)          *((volatile BYTE *)(CMD_IF_RAM_BASE +x))

#define CMD_IF_SLOT_BASE       *((volatile BYTE *)(CMD_IF_BASE + 0x0))
#define CMD_IF_SLOT_ENABLE     *((volatile BYTE *)(CMD_IF_BASE + 0x1))
#define CMD_IF_HANDSHAKE_OUT   *((volatile BYTE *)(CMD_IF_BASE + 0x2))
#define CMD_IF_HANDSHAKE_IN    *((volatile BYTE *)(CMD_IF_BASE + 0x3))
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


class CommandInterface
{
    volatile BYTE *command_buffer;
    volatile BYTE *response_buffer;
    volatile BYTE *status_buffer;

    BYTE seen_handshake;
public:
    CommandInterface();
    ~CommandInterface();
    
    int poll(Event &ev);
    void dump_registers(void);

};

extern CommandInterface cmd_if;
 
void poll_command_interface(Event &ev);

#endif
