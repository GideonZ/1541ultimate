#include "dos.h"

Message c_message_identification_dos = { 20, true, (BYTE *)"ULTIMATE-II DOS V1.0" }; 
Message c_status_not_implemented     = { 27, true, (BYTE *)"99,FUNCTION NOT IMPLEMENTED" };
Message c_status_no_data             = { 19, true, (BYTE *)"81,NOT IN DATA MODE" };
Message c_status_file_not_found      = { 17, true, (BYTE *)"82,FILE NOT FOUND" };
Message c_status_no_such_dir         = { 20, true, (BYTE *)"83,NO SUCH DIRECTORY" };
Message c_status_no_file_to_close    = { 19, true, (BYTE *)"84,NO FILE TO CLOSE" };
Message c_status_file_not_open       = { 15, true, (BYTE *)"85,NO FILE OPEN" };

Dos :: ~Dos()
{
    delete path;
    delete[] data_message.message;
    delete[] status_message.message;
    // officially we should deregister ourselves, but as this will only occur at application exit, there is no need
}
    
void Dos :: parse_command(Message *command, Message **reply, Message **status)
{
    // the first byte of the command lead us here to this function.
    // the second byte is the command byte for the DOS; the third and forth byte are the length
    // data follows after the 4th byte and is thus aligned for copy.    
    UINT transferred = 0;
    FRESULT res = FR_OK;
    
    switch(command->message[1]) {
        case DOS_CMD_IDENTIFY:
            *reply  = &c_message_identification_dos;
            *status = &c_status_ok;
            break;
        case DOS_CMD_OPEN_FILE:
            file = root.fopen((char *)&command->message[3], path->get_path_object(), command->message[2]);
            *reply  = &c_message_empty;
            if(!file) {
                strcpy((char *)status_message.message, FileSystem::get_error_string(root.get_last_error()));
                status_message.length = strlen((char *)status_message.message);
                *status = &status_message; 
            } else {
                *status = &c_status_ok; 
            }
            break;
        case DOS_CMD_CLOSE_FILE:
            *reply  = &c_message_empty;
            if (file) {
                file->close();
                file = NULL;
                *status = &c_status_ok; 
            } else {
                *status = &c_status_no_file_to_close; 
            }
            break;
        case DOS_CMD_CHANGE_DIR:
            *reply  = &c_message_empty;
            command->message[command->length] = 0;
            if (path->cd((char *)&command->message[2])) {
                *status = &c_status_ok;
            } else {
                *status = &c_status_no_such_dir; 
            }        
            break;
        case DOS_CMD_GET_PATH:
            strcpy((char *)data_message.message, path->get_path());
            data_message.length = strlen((char *)data_message.message);
            *reply  = &data_message;
            *status = &c_status_ok; 
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
            if(!file) {
                *reply  = &c_message_empty;
                *status = &c_status_file_not_open;
            } else {
                remaining = (((int)command->message[3]) << 8) | command->message[2];
                dos_state = e_dos_in_file;
                get_more_data(reply, status);
            }
            break;
        case DOS_CMD_WRITE_DATA:
            if(!file) {
                *reply  = &c_message_empty;
                *status = &c_status_file_not_open;
            } else {
                res = file->write(&command->message[4], command->length-4, &transferred);
                *status = &c_status_ok;
                if (res != FR_OK) {
                    strcpy((char *)status_message.message, FileSystem::get_error_string(res));
                    status_message.length = strlen((char *)status_message.message);
                    *status = &status_message;
                }
            }
            break;
        case DOS_CMD_ECHO:
            *reply  = command;
            *status = &c_status_ok;
            break;
        default:
            *reply  = &c_message_empty;
            *status = &c_status_unknown_command; 
    }
}
    
void Dos :: get_more_data(Message **reply, Message **status)
{
    UINT transferred = 0;
    FRESULT res;
    int length;
    
    switch (dos_state) {
    case e_dos_idle:
        printf("DOS: asking for more data, but state is idle\n");
        *reply  = &c_message_empty;
        *status = &c_status_no_data;
        break;
    case e_dos_in_file:
        length = (remaining > 512)?512:remaining;        
        res = file->read(data_message.message, length, &transferred);
        data_message.length = (int)transferred;
        remaining -= length;
        data_message.last_part = (remaining == 0); 
        dos_state = e_dos_idle;
        if (res != FR_OK) {
            strcpy((char *)status_message.message, FileSystem::get_error_string(res));
            status_message.length = strlen((char *)status_message.message);
        }
        break;
    case e_dos_in_directory:
        break;
    default:
        printf("DOS: Illegal state\n");
        dos_state = e_dos_idle;
    }        
}
