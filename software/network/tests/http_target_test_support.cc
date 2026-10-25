#include "command_target.h"

CommandTarget *command_targets[CMD_IF_MAX_TARGET + 1];
Message c_message_no_target = { 9, true, (uint8_t *)"NO TARGET" };
Message c_status_ok = { 5, true, (uint8_t *)"00,OK" };
Message c_status_unknown_command = { 18, true, (uint8_t *)"21,UNKNOWN COMMAND" };
Message c_message_empty = { 0, true, (uint8_t *)"" };
