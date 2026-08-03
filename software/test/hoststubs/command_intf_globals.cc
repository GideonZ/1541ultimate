// Host-test definitions for the globals that command_intf.h declares.
//
// The real definitions live in command_intf.cc, which talks to the memory
// mapped command interface hardware. Host tests only need the registry that
// CommandTarget subclasses register themselves into, and the shared reply and
// status messages that command handlers hand back.

#include "command_intf.h"

CommandTarget *command_targets[64] = { 0 };

Message c_message_empty         = {  0, true, (uint8_t *)"" };
Message c_message_no_target     = { 20, true, (uint8_t *)"99,NO SUCH TARGET   " };
Message c_status_ok             = {  5, true, (uint8_t *)"00,OK" };
Message c_status_unknown_command = { 22, true, (uint8_t *)"99,UNKNOWN COMMAND    " };
