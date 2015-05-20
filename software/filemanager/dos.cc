#include "dos.h"
#include "userinterface.h"

__inline uint32_t cpu_to_32le(uint32_t a)
{
    uint32_t m1, m2;
    m1 = (a & 0x00FF0000) >> 8;
    m2 = (a & 0x0000FF00) << 8;
    return (a >> 24) | (a << 24) | m1 | m2;
}

__inline WORD cpu_to_16le(WORD a)
{
    return (a >> 8) | (a << 8);
}

// crate and register ourselves!
Dos dos1(1);
Dos dos2(2);

Message c_message_identification_dos = { 20, true, (uint8_t *)"ULTIMATE-II DOS V1.0" }; 
Message c_status_directory_empty     = { 18, true, (uint8_t *)"01,DIRECTORY EMPTY" };
Message c_status_truncated           = { 20, true, (uint8_t *)"02,REQUEST TRUNCATED" };
Message c_status_not_implemented     = { 27, true, (uint8_t *)"99,FUNCTION NOT IMPLEMENTED" };
Message c_status_no_data             = { 19, true, (uint8_t *)"81,NOT IN DATA MODE" };
Message c_status_file_not_found      = { 17, true, (uint8_t *)"82,FILE NOT FOUND" };
Message c_status_no_such_dir         = { 20, true, (uint8_t *)"83,NO SUCH DIRECTORY" };
Message c_status_no_file_to_close    = { 19, true, (uint8_t *)"84,NO FILE TO CLOSE" };
Message c_status_file_not_open       = { 15, true, (uint8_t *)"85,NO FILE OPEN" };
Message c_status_cannot_read_dir     = { 23, true, (uint8_t *)"86,CAN'T READ DIRECTORY" }; 
Message c_status_internal_error      = { 17, true, (uint8_t *)"87,INTERNAL ERROR" };
Message c_status_no_information      = { 27, true, (uint8_t *)"88,NO INFORMATION AVAILABLE" };

Dos :: Dos(int id) : directoryList(16, NULL)
{
    command_targets[id] = this;
    data_message.message = new uint8_t[512];
    status_message.message = new uint8_t[80];
    path = file_manager.get_new_path("Dos");
}
   

Dos :: ~Dos()
{
    file_manager.release_path(path);
    delete[] data_message.message;
    delete[] status_message.message;
    // officially we should deregister ourselves, but as this will only occur at application exit, there is no need
}
    
void Dos :: parse_command(Message *command, Message **reply, Message **status)
{
    // the first byte of the command lead us here to this function.
    // the second byte is the command byte for the DOS; the third and forth byte are the length
    // data follows after the 4th byte and is thus aligned for copy.    
    uint32_t pos, addr, len;
    UINT transferred = 0;
    FRESULT res = FR_OK;
    CachedTreeNode *po;
    FileInfo *fi;
    string str;
    
    switch(command->message[1]) {
        case DOS_CMD_IDENTIFY:
            *reply  = &c_message_identification_dos;
            *status = &c_status_ok;
            break;
        case DOS_CMD_OPEN_FILE:
            command->message[command->length] = 0;
            file = file_manager.fopen(path, (char *)&command->message[3], command->message[2]);
            *reply  = &c_message_empty;
            if(!file) {
                strcpy((char *)status_message.message, FileSystem::get_error_string(file_manager.get_last_error()));
                status_message.length = strlen((char *)status_message.message);
                *status = &status_message; 
            } else {
                *status = &c_status_ok; 
            }
            break;
        case DOS_CMD_CLOSE_FILE:
            *reply  = &c_message_empty;
            if (file) {
                file_manager.fclose(file);
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
                pos = (((uint32_t)command->message[5]) << 24) | (((uint32_t)command->message[4]) << 16) |
                      (((uint32_t)command->message[3]) << 8) | command->message[2];
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
            fi = file->info;
            if(!fi) {
                *status = &c_status_no_information;
                break;
            }                    
            dos_info.size   = cpu_to_32le(fi->size);
        	dos_info.date   = cpu_to_16le(fi->date);
        	dos_info.time   = cpu_to_16le(fi->time);
            dos_info.extension[0] = ' ';
            dos_info.extension[1] = ' ';
            dos_info.extension[2] = ' ';
            strncpy(dos_info.extension, fi->extension, 3);
        	dos_info.attrib = fi->attrib; 
        	strncpy(dos_info.filename, fi->lfname, 63);
            data_message.length = strlen(dos_info.filename) + 4 + 2 + 2 + 4;
            data_message.last_part = true;
            memcpy(data_message.message, &dos_info, data_message.length);
            *reply = &data_message;
            break;
        case DOS_CMD_LOAD_REU:
            if(!file) {
                *status = &c_status_file_not_open;
                *reply  = &c_message_empty;
                break;
            } 
            *status = &c_status_ok; 
            addr = (((uint32_t)command->message[5]) << 24) | (((uint32_t)command->message[4]) << 16) |
                   (((uint32_t)command->message[3]) << 8) | command->message[2];
            len  = (((uint32_t)command->message[9]) << 24) | (((uint32_t)command->message[8]) << 16) |
                   (((uint32_t)command->message[7]) << 8) | command->message[6];
            addr &= 0x00FFFFFF;
            len  &= 0x00FFFFFF;
            if ((addr + len) > 0x01000000) {
                len = 0x01000000 - addr;
                *status = &c_status_truncated; 
            }    
            res = file->read((uint8_t *)(addr | 0x01000000), len, &transferred);
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
            addr = (((uint32_t)command->message[5]) << 24) | (((uint32_t)command->message[4]) << 16) |
                   (((uint32_t)command->message[3]) << 8) | command->message[2];
            len  = (((uint32_t)command->message[9]) << 24) | (((uint32_t)command->message[8]) << 16) |
                   (((uint32_t)command->message[7]) << 8) | command->message[6];
            addr &= 0x00FFFFFF;
            len  &= 0x00FFFFFF;
            if ((addr + len) > 0x01000000) {
                len = 0x01000000 - addr;
                *status = &c_status_truncated; 
            }    
            res = file->write((uint8_t *)(addr | 0x01000000), len, &transferred);
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
/* FIXME path
            if(!path->cd(user_interface->get_path()->get_full_path(str))) {
                *status = &c_status_no_such_dir;
                break;
            } // fallthrough
*/
        case DOS_CMD_GET_PATH:
            strcpy((char *)data_message.message, path->get_path());
            data_message.length = strlen((char *)data_message.message);
            data_message.last_part = true;
            *reply  = &data_message;
            *status = &c_status_ok; 
            break;
        case DOS_CMD_OPEN_DIR:
        	cleanupDirectory();
        	dir_entries = path->get_directory(directoryList);
            *reply  = &c_message_empty;
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
    
void Dos :: cleanupDirectory() {
	for (int i=0;i<directoryList.get_elements();i++) {
		delete directoryList[i];
	}
	directoryList.clear_list();
}

void Dos :: get_more_data(Message **reply, Message **status)
{
    UINT transferred = 0;
    FRESULT res;
    int length;
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
        fi = directoryList[current_index];
        if (!fi) {
            *status = &c_status_internal_error;
            *reply = &c_message_empty;
        } else {
			data_message.message[0] = fi->attrib;
            strcpy((char *)&data_message.message[1], fi->lfname);
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
