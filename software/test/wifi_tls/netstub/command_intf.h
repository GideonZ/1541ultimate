// Host stand-in for software/io/command_interface/command_intf.h: the real command_target.h, and of CommandInterface
// only what network_target.cc uses, faked in net_tls_test.cc.
#ifndef COMMAND_INTF_H
#define COMMAND_INTF_H
#include <string.h> // the real header brings it, through its own includes
#include "command_target.h"

class CommandInterface
{
public:
    bool reset_pending(void);
};
extern CommandInterface cmd_if;
#endif
