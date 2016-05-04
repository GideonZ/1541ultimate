#include "userinterface.h"
#include <string.h>
#include <uci_commands.h>
#include "subsys.h"
#include "tape_recorder.h"

__inline uint32_t cpu_to_32le(uint32_t a)
{
    uint32_t m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
}

__inline uint16_t cpu_to_16le(uint16_t a)
{
    return (a >> 8) | (a << 8);
}

// crate and register ourselves!
UCI_Commands cmd(15);

static Message c_message_identification_uci = { 23, true, (uint8_t *)"UCI COMMAND TARGET V1.0" };
static Message c_status_no_data             = { 19, true, (uint8_t *)"81,NOT IN DATA MODE" };


UCI_Commands :: UCI_Commands(int id)
{
    command_targets[id] = this;
    data_message.message = new uint8_t[512];
    status_message.message = new uint8_t[80];
}
   

UCI_Commands :: ~UCI_Commands()
{
    delete[] data_message.message;
    delete[] status_message.message;
    // officially we should deregister ourselves, but as this will only occur at application exit, there is no need
}
    
void UCI_Commands :: parse_command(Message *command, Message **reply, Message **status)
{
    // the first byte of the command lead us here to this function.
    // the second byte is the command byte; the third and forth byte are the length, if applicable
    // data follows after the 4th byte and is thus aligned for copy.    
	SubsysCommand *cmd;

    switch(command->message[1]) {
        case UCI_CMD_IDENTIFY:
            *reply  = &c_message_identification_uci;
            *status = &c_status_ok;
            break;
        case UCI_CMD_ENTER_MENU:
            *reply  = &c_message_identification_uci;
            *status = &c_status_ok;
            break;
        case UCI_CMD_FINISH_CAPTURE:
        	printf("Finish tape capture command!\n");
        	cmd = new SubsysCommand(NULL, SUBSYSID_TAPE_RECORDER, MENU_REC_FINISH, 0, NULL, NULL);
        	cmd->execute();
        	*reply  = &c_message_empty;
            *status = &c_status_ok;
            break;
        default:
            *reply  = &c_message_empty;
            *status = &c_status_unknown_command; 
    }
}
    

void UCI_Commands :: get_more_data(Message **reply, Message **status)
{
	*reply  = &c_message_empty;
	*status = &c_status_no_data;
}

void UCI_Commands :: abort(void) {

}
