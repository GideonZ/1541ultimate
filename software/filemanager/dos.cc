#include "dos.h"

Message c_message_identification_dos = { 20, true, (BYTE *)"ULTIMATE-II DOS V1.0" }; 
Message c_status_not_implemented     = { 27, true, (BYTE *)"99,FUNCTION NOT IMPLEMENTED" };

Dos dos1(1); // this global registers a Dos interface object as ID 1

Dos :: Dos(int id)
{
    command_targets[id] = this; // register ourselves
    state = 0;
    temp_message.message = new BYTE[80];
}

Dos :: ~Dos()
{
    delete[] temp_message.message;
    // officially we should deregister ourselves, but as this will only occur at application exit, there is no need
}
    
void Dos :: parse_command(Message *command, Message **reply, Message **status)
{
    // the first byte of the command lead us here to this function.
    // the second byte is the command byte for the DOS; the third and forth byte are the length
    // data follows after the 4th byte and is thus aligned for copy.    
    switch(command->message[1]) {
        case DOS_CMD_IDENTIFY:
            *reply  = &c_message_identification_dos;
            *status = &c_status_ok;
            break;
        case DOS_CMD_OPEN_FILE:
            *reply  = &c_message_empty;
            *status = &c_status_not_implemented; 
            break;
        case DOS_CMD_CLOSE_FILE:
            *reply  = &c_message_empty;
            *status = &c_status_not_implemented; 
            break;
        case DOS_CMD_CHANGE_DIR:
            *reply  = &c_message_empty;
            *status = &c_status_not_implemented; 
            break;
        case DOS_CMD_GET_PATH:
            *reply  = &c_message_empty;
            *status = &c_status_not_implemented; 
            break;
        case DOS_CMD_OPEN_DIR:
            *reply  = &c_message_empty;
            *status = &c_status_not_implemented; 
            break;
        case DOS_CMD_READ_DIR:
            *reply  = &c_message_empty;
            *status = &c_status_not_implemented; 
            break;
        case DOS_CMD_CLOSE_DIR:
            *reply  = &c_message_empty;
            *status = &c_status_not_implemented; 
            break;
        case DOS_CMD_READ_DATA:
            *reply  = &c_message_empty;
            *status = &c_status_not_implemented; 
            break;
        case DOS_CMD_WRITE_DATA:
            *reply  = &c_message_empty;
            *status = &c_status_not_implemented; 
            break;
        case DOS_CMD_ECHO:
            *reply  = command;
            *status = &c_status_ok;
            break;
        case DOS_CMD_COUNT:
            printf("starting counter\n");
            sprintf((char*)temp_message.message, "STARTING COUNTER");
            temp_message.length = 16;
            temp_message.last_part = false;
            *reply  = &temp_message;
            *status = &c_status_ok;
            state = 1;
            break;
        default:
            *reply  = &c_message_empty;
            *status = &c_status_unknown_command; 
    }
}
    
void Dos :: get_more_data(Message **reply, Message **status)
{
    if (state == 0) {
        printf("asking for more data, but state = 0\n");
        *reply  = &c_message_empty;
        *status = &c_status_ok;
    } else {
        printf("copying count value\n");
        sprintf((char*)temp_message.message, "COUNT: %b", state);
        temp_message.length = 9;
        state ++;
        temp_message.last_part = (state == 9);
        if (state == 9)
            state = 0;
        *reply  = &temp_message;
        *status = &c_message_empty;
    } 
}
