#include "dos.h"
#include "userinterface.h"

// crate and register ourselves!
Dos dos1(1);
Dos dos2(2);

Message c_message_identification_dos = { 20, true, (BYTE *)"ULTIMATE-II DOS V1.0" }; 
Message c_status_directory_empty     = { 18, true, (BYTE *)"01,DIRECTORY EMPTY" };
Message c_status_truncated           = { 20, true, (BYTE *)"02,REQUEST TRUNCATED" };
Message c_status_not_implemented     = { 27, true, (BYTE *)"99,FUNCTION NOT IMPLEMENTED" };
Message c_status_no_data             = { 19, true, (BYTE *)"81,NOT IN DATA MODE" };
Message c_status_file_not_found      = { 17, true, (BYTE *)"82,FILE NOT FOUND" };
Message c_status_no_such_dir         = { 20, true, (BYTE *)"83,NO SUCH DIRECTORY" };
Message c_status_no_file_to_close    = { 19, true, (BYTE *)"84,NO FILE TO CLOSE" };
Message c_status_file_not_open       = { 15, true, (BYTE *)"85,NO FILE OPEN" };
Message c_status_cannot_read_dir     = { 23, true, (BYTE *)"86,CAN'T READ DIRECTORY" }; 
Message c_status_internal_error      = { 17, true, (BYTE *)"87,INTERNAL ERROR" };
Message c_status_no_information      = { 27, true, (BYTE *)"88,NO INFORMATION AVAILABLE" };

Dos :: Dos(int id)
{
    command_targets[id] = this;
    data_message.message = new BYTE[512];
    status_message.message = new BYTE[80];
    path = new Path();
}
   

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
    DWORD pos, addr, len;
    UINT transferred = 0;
    FRESULT res = FR_OK;
    PathObject *po;
    FileInfo *fi;
    string str;
    
    switch(command->message[1]) {
        case DOS_CMD_IDENTIFY:
            *reply  = &c_message_identification_dos;
            *status = &c_status_ok;
            break;
        case DOS_CMD_OPEN_FILE:
            command->message[command->length] = 0;
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
                root.fclose(file);
                file = NULL;
                *status = &c_status_ok; 
            } else {
                *status = &c_status_no_file_to_close; 
            }
            break;
        case DOS_CMD_FILE_SEEK:
            *reply  = &c_message_empty;
            *status = &c_status_ok; 
            if(!file) {
                *status = &c_status_file_not_open;
            } else {
                pos = (((DWORD)command->message[5]) << 24) | (((DWORD)command->message[4]) << 16) |
                      (((DWORD)command->message[3]) << 8) | command->message[2];
                res = file->seek(pos);
                if (res != FR_OK) {
                    strcpy((char *)status_message.message, FileSystem::get_error_string(res));
                    status_message.length = strlen((char *)status_message.message);
                    *status = &status_message;
                }
            }
            break;
        case DOS_CMD_FILE_INFO:
            *reply  = &c_message_empty;
            *status = &c_status_ok; 
            if(!file) {
                *status = &c_status_file_not_open;
                break;
            } 
            po = file->node;
            if(po) {
                fi = po->get_file_info();
            }
            if(!po || !fi) {
                *status = &c_status_no_information;
                break;
            }                    
            dos_info.size   = fi->size;
        	dos_info.date   = fi->date;
        	dos_info.time   = fi->time;
            dos_info.extension[0] = ' ';
            dos_info.extension[1] = ' ';
            dos_info.extension[2] = ' ';
            strncpy(dos_info.extension, fi->extension, 3);
        	dos_info.attrib = fi->attrib; 
        	strncpy(dos_info.filename, fi->lfname, 63);
            data_message.length = strlen(dos_info.filename) + 4 + 2 + 2 + 4;
            data_message.last_part = true;
            data_message.message = (BYTE *)&dos_info;
            *reply = &data_message;
            break;
        case DOS_CMD_LOAD_REU:
            if(!file) {
                *status = &c_status_file_not_open;
                *reply  = &c_message_empty;
                break;
            } 
            *status = &c_status_ok; 
            addr = (((DWORD)command->message[5]) << 24) | (((DWORD)command->message[4]) << 16) |
                   (((DWORD)command->message[3]) << 8) | command->message[2];
            len  = (((DWORD)command->message[9]) << 24) | (((DWORD)command->message[8]) << 16) |
                   (((DWORD)command->message[7]) << 8) | command->message[6];
            addr &= 0x00FFFFFF;
            len  &= 0x00FFFFFF;
            if ((addr + len) > 0x01000000) {
                len = 0x01000000 - addr;
                *status = &c_status_truncated; 
            }    
            res = file->read((BYTE *)(addr | 0x01000000), len, &transferred);
            *reply  = &data_message;
            sprintf((char *)data_message.message, "$%6x BYTES LOADED TO REU $%6x", transferred, addr);
            data_message.length = 35;
            data_message.last_part = true;
            if (res != FR_OK) {
                strcpy((char *)status_message.message, FileSystem::get_error_string(res));
                status_message.length = strlen((char *)status_message.message);
                *status = &status_message;
            }
            break;
        case DOS_CMD_SAVE_REU:
            if(!file) {
                *status = &c_status_file_not_open;
                *reply  = &c_message_empty;
                break;
            } 
            *status = &c_status_ok; 
            addr = (((DWORD)command->message[5]) << 24) | (((DWORD)command->message[4]) << 16) |
                   (((DWORD)command->message[3]) << 8) | command->message[2];
            len  = (((DWORD)command->message[9]) << 24) | (((DWORD)command->message[8]) << 16) |
                   (((DWORD)command->message[7]) << 8) | command->message[6];
            addr &= 0x00FFFFFF;
            len  &= 0x00FFFFFF;
            if ((addr + len) > 0x01000000) {
                len = 0x01000000 - addr;
                *status = &c_status_truncated; 
            }    
            res = file->write((BYTE *)(addr | 0x01000000), len, &transferred);
            *reply  = &data_message;
            sprintf((char *)data_message.message, "$%6x BYTES SAVED FROM REU $%6x", transferred, addr);
            data_message.length = 36;
            data_message.last_part = true;
            if (res != FR_OK) {
                strcpy((char *)status_message.message, FileSystem::get_error_string(res));
                status_message.length = strlen((char *)status_message.message);
                *status = &status_message;
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
        case DOS_CMD_COPY_UI_PATH:
            *reply  = &c_message_empty;
            if(!path->cd(user_interface->get_path()->get_full_path(str))) {
                *status = &c_status_no_such_dir;
                break;
            } // fallthrough
        case DOS_CMD_GET_PATH:
            strcpy((char *)data_message.message, path->get_path());
            data_message.length = strlen((char *)data_message.message);
            data_message.last_part = true;
            *reply  = &data_message;
            *status = &c_status_ok; 
            break;
        case DOS_CMD_OPEN_DIR:
            po = path->get_path_object();
            *reply  = &c_message_empty;
            po->cleanup_children();
            dir_entries = po->fetch_children(); // children.get_elements(); 
            if (dir_entries < 0) {
                *status = &c_status_cannot_read_dir; 
            } else if(dir_entries == 0) {
                *status = &c_status_directory_empty;
            } else {
                *status = &c_status_ok;
            }            
            break;
        case DOS_CMD_READ_DIR:
            current_index = 0;
            dos_state = e_dos_in_directory;
            get_more_data(reply, status);
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
            *reply  = &c_message_empty;
            if(!file) {
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
            command->last_part = true;
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
    PathObject *po, *entry;
    FileInfo *fi;
    
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
        remaining -= transferred;
        if ((transferred != length) || (remaining == 0)) {
            data_message.last_part = true;
            dos_state = e_dos_idle;
        } else {
            data_message.last_part = false;
        }
        if (res != FR_OK) {
            strcpy((char *)status_message.message, FileSystem::get_error_string(res));
            status_message.length = strlen((char *)status_message.message);
        } else {
            *status = &c_message_empty;
        }
        *reply = &data_message;
        break;
    case e_dos_in_directory:
        po = path->get_path_object();
        entry = po->children[current_index];
        if (!entry) {
            *status = &c_status_internal_error;
            *reply = &c_message_empty;
        } else {
            fi = entry->get_file_info();
            if (fi) {
                data_message.message[0] = fi->attrib;
            } else {
                data_message.message[0] = 0x00;
            }                
            strcpy((char *)&data_message.message[1], entry->get_name());
            data_message.length = 1+strlen((char*)&data_message.message[1]);
            current_index++;
            if (current_index == dir_entries) {
                data_message.last_part = true; 
                *status = &c_status_ok;
                dos_state = e_dos_idle;
            } else {
                *status = &c_message_empty;
                data_message.last_part = false;
            }
            *reply = &data_message;
        }
        break;
    default:
        printf("DOS: Illegal state\n");
        dos_state = e_dos_idle;
    }        
}

void Dos :: abort(void) {
    dos_state = e_dos_idle;
}
