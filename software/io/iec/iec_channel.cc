#include "iec_channel.h"
#include "dump_hex.h"

IecChannel :: IecChannel(IecInterface *intf, int ch)
{
    fm = FileManager :: getFileManager();
    interface = intf;
    channel = ch;
    f = NULL;
    pointer = 0;
    write = 0;
    state = e_idle;
    dir_index = 0;
    dir_last = 0;
    bytes = 0;
    last_byte = 0;
    size = 0;
    last_command = 0;
    prefetch = 0;
    prefetch_max = 0;
    dirPartition = NULL;
    append = false;
}

IecChannel :: ~IecChannel()
{
    close_file();
}

void IecChannel :: reset(void)
{
    close_file();

    pointer = 0;
    write = 0;
    dir_index = 0;
    dir_last = 0;
    bytes = 0;
    last_byte = 0;
    size = 0;
    last_command = 0;
    prefetch = 0;
    prefetch_max = 0;
    dirPartition = NULL;
    append = false;
}

void IecChannel :: reset_prefetch(void)
{
#if IECDEBUG
    printf("(R)");
#endif
    prefetch = pointer;
}

int IecChannel :: prefetch_data(uint8_t& data)
{
    if (state == e_error) {
        return IEC_NO_FILE;
    }
    if (prefetch == last_byte) {
        data = buffer[prefetch];
        prefetch++;
        return IEC_LAST;
    }
    if (prefetch < prefetch_max) {
        data = buffer[prefetch];
        prefetch++;
        return IEC_OK;
    }
    if (prefetch > prefetch_max) {
        return IEC_NO_FILE;
    }
    return IEC_BUFFER_END;
}

int IecChannel :: pop_data(void)
{
    switch(state) {
        case e_file:
            if(pointer == last_byte) {
                state = e_complete;
                return IEC_NO_FILE; // no more data?
            } else if(pointer == 255) {
                if(read_block())  // also resets pointer.
                    return IEC_READ_ERROR;
                else
                    return IEC_OK;
            }
            break;
        case e_dir:
            if(pointer == last_byte) {
                state = e_complete;
                return IEC_NO_FILE; // no more data?
            } else if(pointer == 31) {
                while(read_dir_entry())
                    ;
                return IEC_OK;
            }
            break;
        default:
            return IEC_NO_FILE;
    }

    pointer++;
    return IEC_OK;
}

int IecChannel :: read_dir_entry(void)
{
    if(dir_index >= dir_last) {
        buffer[2] = 9999 & 255;
        buffer[3] = 9999 >> 8;
        memcpy(&buffer[4], "BLOCKS FREE.             \0\0\0", 28);
        last_byte = 31;
        pointer = 0;
        prefetch_max = 31;
        prefetch = 0;
        return 0;
    }
    const char *iecName = dirPartition->GetIecName(dir_index);

    // printf("Dir index = %d %s\n", dir_index, iecName);

    if (!pattern_match(dirpattern.c_str(), iecName, false)) {
        dir_index++;
        return 1;
    }
    FileInfo *info = dirPartition->GetSystemFile(dir_index);

    //printf("info = %p\n", info);
    uint32_t size = 0;
    uint32_t size2 = 0;
    if(info) {
        size = info->size;
    }
    size = (size + 253) / 254;
    if(size > 9999)
        size = 9999;
    size2 = size;
    int chars=1;
    while(size2 >= 10) {
        size2 /= 10;
        chars ++;
    }
    int spaces=3-chars;
    buffer[2] = size & 255;
    buffer[3] = size >> 8;
    int pos = 4;
    while((spaces--)>=0)
        buffer[pos++] = 32;
    buffer[pos++]=34;
    const char *name = iecName;
    const char *src = name+3;
    while(*src)
        buffer[pos++] = *(src++);
    buffer[pos++] = 34;
    while(pos < 32)
        buffer[pos++] = 32;

    if (info->is_directory()) {
        memcpy(&buffer[27-chars], "DIR", 3);
    } else {
        memcpy(&buffer[27-chars], name, 3);
    }

    buffer[31] = 0;
    pointer = 0;
    prefetch_max = 32;
    prefetch = 0;
    dir_index ++;
    return 0;
}

int IecChannel :: read_block(void)
{
    FRESULT res = FR_DENIED;
    uint32_t bytes;
    if(f) {
        res = f->read(buffer, 256, &bytes);
//        printf("\nSize was %d. Read %d bytes. ", size, bytes);
    }
    if(res != FR_OK) {
        state = e_error;
        return IEC_READ_ERROR;
    }
    if(bytes == 0)
        state = e_complete; // end should have triggered already

    if(bytes != 256) {
        last_byte = int(bytes)-1;
    } else if(size == 256) {
        last_byte = 255;
    }
    size -= 256;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 256;
    return 0;
}

int IecChannel :: push_data(uint8_t b)
{
    uint32_t bytes;

    switch(state) {
        case e_filename:
            if(pointer < 64)
                buffer[pointer++] = b;
            break;
        case e_file:
            buffer[pointer++] = b;
            if(pointer == 256) {
                FRESULT res = FR_DENIED;
                if(f) {
                    res = f->write(buffer, 256, &bytes);
                }
                if(res != FR_OK) {
                    fm->fclose(f);
                    f = NULL;
                    state = e_error;
                    return IEC_WRITE_ERROR;
                }
                pointer = 0;
            }
            break;

        default:
            return IEC_BYTE_LOST;
    }
    return IEC_OK;
}

int IecChannel :: push_command(uint8_t b)
{
    if(b)
        last_command = b;

    switch(b) {
        case 0xF0: // open
            close_file();
            state = e_filename;
            pointer = 0;
            break;
        case 0xE0: // close
            printf("close %d %d\n", pointer, write);
            if(write) {
                //dump_hex(buffer, pointer);
                if (f) {
                    if (pointer > 0) {
                        FRESULT res = f->write(buffer, pointer, &bytes);
                        if (res != FR_OK) {
                            state = e_error;
                            return IEC_WRITE_ERROR;
                        }
                    }
                    close_file();
                } else {
                    state = e_error;
                    return IEC_WRITE_ERROR;
                }
            }
            state = e_idle;
            break;
        case 0x60:
            reset_prefetch();
            break;
        case 0x00: // end of data
            if(last_command == 0xF0)
                open_file();
            break;
        default:
            printf("Error on channel %d. Unknown command: %b\n", channel, b);
    }
    return IEC_OK;
}

void IecChannel :: parse_command(char *buffer, command_t *command)
{
    // initialize
    for(int i=0;i<5;i++) {
        command->names[i].name = 0;
        command->names[i].drive = -1;
        command->names[i].separator = ' ';
    }
    command->digits = -1;
    command->cmd = buffer;

    // First strip off any carriage returns, in any case
    int len = strlen(buffer);
    while ((len > 0) && (buffer[len-1] == 0x0d)) {
        len--;
        buffer[len] = 0;
    }

    // look for , and split
    int idx = 0;
    command->names[0].name = buffer;
    for(int i=0;i<len;i++) {
        if ((buffer[i] == ',')||(buffer[i] == '=')) {
            idx++;
            if (idx == 5) {
                break;
            }
            command->names[idx].name = buffer + i + 1;
            command->names[idx].separator = buffer[i];
            buffer[i] = 0;
        }
    }

    // look for : and split
    for(int j=0;j<5;j++) {
        char *s = command->names[j].name;
        if (!s) {
            break;
        }
        len = strlen(s);
        for(int i=0;i<len;i++) {
            if (s[i] == ':') {
                s[i] = 0;
                command->names[j].name = s + i + 1;
                // now snoop off the digits and place them into drive number
                int mult = 1;
                int dr = 0;
                while(isdigit(s[--i])) {
                    dr += (s[i] - '0') * mult;
                    mult *= 10;
                    s[i] = 0;
                }
                if (mult > 1) { // digits found
                    command->names[j].drive = dr;
                }
                break;
            }
        }
    }
    if (command->names[0].name == command->cmd) {
        command->names[0].name = 0;
        int mult = 1;
        int dr = 0;
        char *s = command->cmd;
        int i = strlen(s);
        while(isdigit(s[--i])) {
            dr += (s[i] - '0') * mult;
            mult *= 10;
            //s[i] = 0; // we should not snoop them off; they may be part of a filename
        }
        if (mult > 1) { // digits found
            command->digits = dr;
        }
    }
}

void IecChannel :: dump_command(command_t& cmd)
{
#if IECDEBUG
    printf("IEC Command = '%s'. Arguments:\n", cmd.cmd);
    for(int i=0;i<5;i++) {
        char *n = cmd.names[i].name;
        printf("  Arg %d: '%s' [%c] (%d)\n", i, n?n:"(null)", cmd.names[i].separator, cmd.names[i].drive);
    }
#endif
}

bool IecChannel :: hasIllegalChars(const char *name)
{
    int len = strlen(name);
    for(int i=0;i<len;i++) {
        switch(name[i]) {
        case '/': return true;
        case '$': return true;
        case '?': return true;
        case '*': return true;
        case ',': return true;
        default: break;
        }
    }
    return false;
}

const char *IecChannel :: GetExtension(char specifiedType, bool &explicitExt)
{
    explicitExt = false;
    const char *extension = "";
    switch(specifiedType) {
    case 'P':
        extension = ".prg";
        explicitExt = true;
        break;
    case 'S':
        extension = ".seq";
        explicitExt = true;
        break;
    case 'U':
        extension = ".usr";
        explicitExt = true;
        break;
    case 'D':
        extension = ".dir";
        explicitExt = true;
        break;
    default:
        printf("Unknown filetype: %c\n", specifiedType);
    }
    return extension;
}

void IecChannel :: fix_filename(void)
{
    // Temporary fix.. replace all slashes with -
    for (int i=0;i<pointer;i++) {
        if (buffer[i] == '/') {
            buffer[i] = '-';
        }
    }
}

void IecChannel :: parse_filename(command_t& command)
{
    parse_command((char *)buffer, &command);
    dump_command(command);


    // First determine the direction
    append = 0;
    if(channel == 1)
        write = 1;
    else
        write = 0;

    int tp = 1;
    if (command.names[1].name) {
        switch(command.names[1].name[0]) {
        case 'R':
            write = 0;
            tp = 2;
            break;
        case 'W':
            write = 1;
            tp = 2;
            break;
        case 'A':
            write = 1;
            append = 1;
            tp = 2;
            break;
        default:
            printf("Unknown direction: %c\n", command.names[1].name[0]);
        }
    }

    if ((command.names[2].name) && (tp == 1)) {
        switch(command.names[2].name[0]) {
        case 'R':
            write = 0;
            break;
        case 'W':
            write = 1;
            break;
        case 'A':
            write = 1;
            append = 1;
            break;
        default:
            printf("Unknown direction: %c\n", command.names[2].name[0]);
        }
    }

    // Then determine the type (or extension) to be used
    explicitExt = false;
    if(channel < 2) {
        extension = ".prg";
    } else if (write) {
        extension = ".seq";
    } else {
        extension = ".???";
    }

    if (command.names[tp].name) {
        extension = GetExtension(command.names[1].name[0], explicitExt);
    }

    const char *first = (command.names[0].name) ? command.names[0].name : "*";
    printf("Filename after parsing: '%s' %d:%s. Extension = '%s'. Write = %d\n", command.cmd, command.digits, first, extension, write);

    dirPartition = interface->vfs->GetPartition(command.names[0].drive);
}

int IecChannel :: setup_directory_read(command_t &command)
{
    if (explicitExt) {
        dirpattern = &extension[1]; // skip the .
    } else {
        dirpattern = "???";
    }
    const char *first = (command.names[0].name) ? command.names[0].name : "*";
    dirpattern += first;
    printf("IEC Channel: Opening directory... (Pattern = %s)\n", dirpattern.c_str());
    if (dirPartition->ReadDirectory()) {
        interface->get_command_channel()->set_error(ERR_DRIVE_NOT_READY, dirPartition->GetPartitionNumber());
        return 0;
    }
    dir_last = dirPartition->GetDirItemCount();
    state = e_dir;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 32;
    last_byte = -1;
    size = 32;
    memcpy(buffer, c_header, 32);
    buffer[4] = (uint8_t)dirPartition->GetPartitionNumber();
    const char *name = dirPartition->GetFullPath();
    int pos = 8;
    while((pos < 23) && (*name))
        buffer[pos++] = toupper(*(name++));
    dump_hex(buffer, 32);
    dir_index = 0;
    return 0;
}

int IecChannel :: setup_file_access(command_t& command)
{
    flags = FA_READ;

    bool replace = false;

    const char *rawName;
    // If a drive number is given, there is a : in the name, and therefore the replacement @ will end up in the command
    // and the filename ends up in names[0]. In case no drive number is given, the filename is inside of the command, and may still be preceded with @.
    // In both cases, the @ ends up in the first char of the command!
    rawName = command.cmd; // by default we assume that the filename goes in the command
    if (*rawName == '@') {
        replace = true;
        rawName ++; // for now we assume that the filename goes after the @.
    }
    // if a : was present, the actual filename (without @) appears in names[0]
    if (command.names[0].name) {
        rawName = command.names[0].name;
    }

    partition = interface->vfs->GetPartition(command.names[0].drive);

    partition->ReadDirectory();
    int pos = partition->FindIecName(rawName, extension, false);

    char temp_fn[32];

    if(append) {
        if (pos < 0) {
            interface->get_command_channel()->set_error(ERR_FILE_NOT_FOUND, dirPartition->GetPartitionNumber());
            state = e_error;
            return 0;
        } else {
            FileInfo *info = partition->GetSystemFile(pos);
            strncpy(filename, info->lfname, 64);
        }
    } else if(write) {
        if ((pos >= 0) && (!replace)) { // file exists, and not allowed to overwrite
            interface->get_command_channel()->set_error(ERR_FILE_EXISTS, dirPartition->GetPartitionNumber());
            state = e_error;
            return 0;
        } else {
            strcpy(filename, rawName);
            strcat(filename, extension);
        }
    } else { // read
        if (pos < 0) {
            interface->get_command_channel()->set_error(ERR_FILE_NOT_FOUND, dirPartition->GetPartitionNumber());
            state = e_error;
            return 0;
        }
        FileInfo *info = partition->GetSystemFile(pos);
        strncpy(filename, info->lfname, 64);
    }

    flags = (write)?(FA_WRITE|FA_CREATE_NEW|FA_CREATE_ALWAYS):(FA_READ);
    if (replace) {
        flags &= ~FA_CREATE_NEW;
    }
    if (append) {
        flags &= ~(FA_CREATE_ALWAYS | FA_CREATE_NEW);
    }
    state = e_error; // assume something goes wrong ;)
    return 0; //?
}

int IecChannel :: init_iec_transfer(void)
{
    printf("Successfully opened file %s in %s\n", buffer, partition->GetFullPath());
    size = 0;
    if (append) {
        FRESULT fres = f->seek(f->get_size());
        if (fres != FR_OK) {
            interface->get_command_channel()->set_error(ERR_FRESULT_CODE, fres);
            state = e_error;
            return 0;
        }
    }
    last_byte = -1;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 256;
    state = e_file;
    if(!write) {
        size = f->get_size();
        return read_block();
    }
    return 0;
}

int IecChannel :: open_file(void)  // name should be in buffer
{
    buffer[pointer] = 0; // string terminator
    printf("Open file. Raw Filename = '%s'\n", buffer);

    filename[0] = 0;
    fix_filename();
    command_t command;
    parse_filename(command);

    if(command.cmd[0] == '$') {
        return setup_directory_read(command);
    }
    setup_file_access(command);

    if (!filename) {
        printf("Filename not set!\n");
        return 0;
    }
    FRESULT fres = fm->fopen(partition->GetPath(), filename, flags, &f);
    if(f) {
        return init_iec_transfer();
    } else {
        printf("Can't open file %s in %s: %s\n", buffer, partition->GetFullPath(), FileSystem :: get_error_string(fres));
    }
    return 0;
}

int IecChannel :: close_file(void) // file should be open
{
    if(f)
        fm->fclose(f);
    f = NULL;
    state = e_idle;
    return 0;
}

int IecChannel :: ext_open_file(const char *name)
{
    strncpy((char *)buffer, name, 255);
    buffer[255] = 0;
    pointer = strlen((char *)buffer);

    int result = open_file();
    if (state == e_dir || state == e_file) {
        return 1;
    }
    return 0;
}

int IecChannel :: ext_close_file(void)
{
    return push_command(0xE0);
}

/******************************************************************************
 * Command Channel
 ******************************************************************************/

IecCommandChannel :: IecCommandChannel(IecInterface *intf, int ch) : IecChannel(intf, ch)
{
    wr_pointer = 0;
}

IecCommandChannel :: ~IecCommandChannel()
{

}

void IecCommandChannel :: reset(void)
{
    IecChannel::reset();
    set_error(ERR_DOS);
}

void IecCommandChannel :: set_error(int err, int track, int sector)
{
    if(err >= 0) {
        interface->set_error(err, track, sector);
        state = e_idle;
    }
}

void IecCommandChannel :: get_error_string(void)
{
    last_byte = interface->get_error_string((char *)buffer) - 1;
    //printf("Get last error: last = %d. buffer = %s.\n", last_byte, buffer);
    pointer = 0;
    prefetch = 0;
    prefetch_max = last_byte;

    interface->set_error(0, 0, 0);
}

void IecCommandChannel :: talk(void)
{
    if (state != e_status) {
        get_error_string();
        state = e_status;
    }
}

int IecCommandChannel :: pop_data(void)
{
    if (state == e_status) {
        if(pointer > last_byte) {
            state = e_idle;
            return IEC_NO_FILE;
        }
        if(pointer == last_byte) {
            state = e_idle;
            return IEC_LAST;
        }
        pointer++;
        return IEC_OK;
    }
    return IEC_NO_FILE;
}

int IecCommandChannel :: push_data(uint8_t b)
{
    if(wr_pointer < 64) {
        wr_buffer[wr_pointer++] = b;
        return IEC_OK;
    }
    return IEC_BYTE_LOST;
}

void IecCommandChannel :: mem_read(void)
{
    int addr = (int)buffer[3];
    addr |= ((int)buffer[4]) << 8;
    int len = (int)buffer[5];

    memcpy(buffer, interface->getRam() + addr, len);

    last_byte = len - 1;
    pointer = 0;
    prefetch = 0;
    prefetch_max = last_byte;
    set_error(ERR_OK);

}

void IecCommandChannel :: mem_write(void)
{
    int addr = (int)buffer[3];
    addr |= ((int)buffer[4]) << 8;
    int len = (int)buffer[5];

    // maximize to the number of bytes actually received
    if (len > (pointer - 6)) {
        len = pointer - 6;
    }

    memcpy(interface->getRam() + addr, buffer + 6, len);

    set_error(ERR_OK);
}

void IecCommandChannel :: exec_command(command_t &command)
{
    IecPartition *p;

    if (strcmp(command.cmd, "CD") == 0) {
        p = interface->vfs->GetPartition(command.digits);
        if (!command.names[0].name) {
            set_error(ERR_SYNTAX_ERROR_CMD);
        } else if (p->cd(command.names[0].name)) {
            set_error(ERR_OK);
        } else {
            set_error(ERR_DIRECTORY_ERROR);
        }
    } else if (strncmp(command.cmd, "CP", 2) == 0) {
        if (command.digits < 0) {
            set_error(ERR_SYNTAX_ERROR_CMD);
        } else if (command.digits >= MAX_PARTITIONS) {
            set_error(ERR_SYNTAX_ERROR_CMD);
        } else { // partition number found
            p = interface->vfs->GetPartition(command.digits);
            if (p->IsValid()) {
                set_error(ERR_PARTITION_OK, command.digits);
                interface->vfs->SetCurrentPartition(command.digits);
            } else {
                set_error(ERR_PARTITION_ERROR, command.digits);
            }
        }
    } else if (strcmp(command.cmd, "MD")== 0) {
        p = interface->vfs->GetPartition(command.digits);
        if (!p->IsValid()) {
            set_error(ERR_PARTITION_ERROR, command.digits);
        } else if (!command.names[0].name) {
            set_error(ERR_SYNTAX_ERROR_CMD);
        } else if (hasIllegalChars(command.names[0].name)) {
            set_error(ERR_SYNTAX_ERROR_CMD);
        } else if (p->MakeDirectory(command.names[0].name)) {
            set_error(ERR_OK);
        } else {
            set_error(ERR_DIRECTORY_ERROR);
        }
    } else if (strcmp(command.cmd, "RD")== 0) {
        p = interface->vfs->GetPartition(command.digits);
        int f = p->Remove(command, true);
        if (f < 0) {
            set_error(ERR_SYNTAX_ERROR_CMD);
        } else {
            set_error(ERR_FILES_SCRATCHED, f);
        }
    } else if (strncmp(command.cmd, "SCRATCH", strlen(command.cmd))== 0) {
        p = interface->vfs->GetPartition(command.digits);
        int f = p->Remove(command, false);
        if (f < 0) {
            set_error(ERR_SYNTAX_ERROR_CMD);
        } else {
            set_error(ERR_FILES_SCRATCHED, f);
        }
    } else if (strncmp(command.cmd, "RENAME", strlen(command.cmd)) == 0) {
        renam(command);
    } else if (strncmp(command.cmd, "COPY", strlen(command.cmd)) == 0) {
        copy(command);
    } else if (strncmp(command.cmd, "INITIALIZE", strlen(command.cmd)) == 0) {
        set_error(ERR_OK);
    } else if (strcmp(command.cmd, "UI") == 0) {
        set_error(ERR_DOS);
    } else { // unknown command
        set_error(ERR_SYNTAX_ERROR_CMD);
    }
}

void IecCommandChannel :: renam(command_t& command)
{
    char fromName[32];
    int  fromPart;
    char toName[32];
    int  toPart;

    bool dummy;
    bool keepExtension = false;
    int fromAt;
    if ((!command.names[1].name) || (!command.names[0].name)) {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return;
    }

    strcpy(toName, command.names[0].name);
    toPart = command.names[0].drive;

    if (command.names[1].separator == '=') { // no , extension on "TO" name, so we copy the extension of the from name
        keepExtension = true;
        fromAt = 1;
    } else if (command.names[2].separator == '=') { // TO name has extension specified
        strcat(toName, GetExtension(command.names[1].name[0], dummy));
        fromAt = 2;
    } else {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return;
    }

    const char *fromExt = "???";
    strcpy(fromName, command.names[fromAt].name);
    fromPart = command.names[fromAt].drive;

    // does the from name have an extension specified?
    if (command.names[fromAt+1].name) { // yes!
        const char *ext = GetExtension(command.names[fromAt+1].name[0], dummy);
        if (dummy) {
            fromExt = ext;
        }
    }

    printf("From: %d:%s To: %d:%s Keep Extension:%d\n", fromPart, fromName, toPart, toName, keepExtension);

    IecPartition *partitionFrom = interface->vfs->GetPartition(fromPart);
    partitionFrom->ReadDirectory();
    int posFrom = partitionFrom->FindIecName(fromName, fromExt, true);
    if (posFrom < 0) {
        set_error(ERR_FILE_NOT_FOUND, 0);
        return;
    }
    FileInfo *fromInfo = partitionFrom->GetSystemFile(posFrom);
    // Yey, we finally got the actual file we need to rename!!

    if ((keepExtension) && !(fromInfo->attrib & AM_DIR)) {
        set_extension(toName, fromInfo->extension, 32);
    }

    IecPartition *partitionTo = interface->vfs->GetPartition(toPart);

    // Just try the actual rename by the filesystem
    FRESULT fres = fm->rename(partitionFrom->GetPath(), fromInfo->lfname, partitionTo->GetPath(), toName);
    if (fres == FR_OK) {
        set_error(ERR_OK, 0);
        return;
    } else if (fres == FR_EXIST) {
        set_error(ERR_FILE_EXISTS, 0);
        return;
    }

    set_error(ERR_FRESULT_CODE, fres);
}

void IecCommandChannel :: copy(command_t& command)
{
    if ((!command.names[1].name) || (!command.names[0].name)) {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return;
    }
    if (command.names[1].separator != '=') {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return;
    }

    FileInfo *infos[4] = { 0, 0, 0, 0 };
    // First try to find all (possibly) four sources
    for(int i=1;i<5;i++) {
        if (!command.names[i].name) {
            break;
        }
        IecPartition *partitionFrom = interface->vfs->GetPartition(command.names[i].drive);
        partitionFrom->ReadDirectory();
        int posFrom = partitionFrom->FindIecName(command.names[i].name, "???", false);
        if (posFrom < 0) {
            set_error(ERR_FILE_NOT_FOUND, i);
            return;
        }
        FileInfo *info = partitionFrom->GetSystemFile(posFrom);
        infos[i-1] = info;
        if (info->attrib & AM_DIR) {
            set_error(ERR_FILE_TYPE_MISMATCH, i);
            return;
        }
    }

    // Now try to create the output file
    IecPartition *partitionTo = interface->vfs->GetPartition(command.names[0].drive);
    char toName[32];
    strcpy(toName, command.names[0].name);
    // Take the extension from the first file to copy
    set_extension(toName, infos[0]->extension, 32);

    File *fo;
    FRESULT fres = fm->fopen(partitionTo->GetPath(), toName, (FA_WRITE|FA_CREATE_NEW|FA_CREATE_ALWAYS), &fo);
    if (fres == FR_EXIST) {
        set_error(ERR_FILE_EXISTS, 0);
        return;
    } else if(fres != FR_OK) {
        set_error(ERR_FRESULT_CODE, fres);
        return;
    }
    int blocks = 0;
    uint8_t buffer[512];

    if(fo) {
        // okay, the output file is now finally open, let's read the input files and write them to the output file
        for (int i=0;i<4;i++) {
            if (!infos[i]) {
                break;
            }
            IecPartition *partitionFrom = interface->vfs->GetPartition(command.names[i+1].drive);
            File *fi;
            FRESULT fres = fm->fopen(partitionFrom->GetPath(), infos[i]->lfname, FA_READ, &fi);
            if (fres != FR_OK) {
                fm->fclose(fo);
                set_error(ERR_FRESULT_CODE, fres);
                return;
            }
            if (fi) {
                uint32_t transferred = 1;
                while(transferred) {
                    fi->read(buffer, 512, &transferred);
                    if(transferred) {
                        fo->write(buffer, transferred, &transferred);
                        blocks ++;
                    }
                }
                fm->fclose(fi);
            }
        }
        fm->fclose(fo);
    }
    set_error(ERR_OK, 0, blocks);
}

int IecCommandChannel :: ext_open_file(const char *filenameOrCommand)
{
    strncpy((char *)wr_buffer, filenameOrCommand, 63);
    wr_buffer[63] = 0;
    wr_pointer = strlen((char *)wr_buffer);
    push_command(0x60);
    return push_command(0x00);
}

int IecCommandChannel :: ext_close_file(void)
{
    // Do nothing
    return IEC_OK;
}

int IecCommandChannel :: push_command(uint8_t b)
{
    command_t command;

    switch(b) {
        case 0x60:
            reset_prefetch();
            break;
        case 0xE0:
        case 0xF0:
            pointer = 0;
            wr_pointer = 0;
            break;
        case 0x00: // end of data, command received in buffer
            wr_buffer[wr_pointer]=0;
            if (wr_pointer) {
                if (strncmp((char *)wr_buffer, "M-R", 3) == 0) {
                    mem_read();
                    break;
                } else if (strncmp((char *)wr_buffer, "M-W", 3) == 0) {
                    mem_write();
                    break;
                }
                parse_command((char *)wr_buffer, &command);
                dump_command(command);
                exec_command(command);
            }
            break;
        default:
            printf("Error on channel %d. Unknown command: %b\n", channel, b);
    }
    return IEC_OK;
}

void IecPartition :: SetInitialPath(void)
{
    char temp[120];
    char temp2[8];

    strcpy(temp, vfs->GetRootPath());
    if (temp[strlen(temp)-1] != '/') {
        strcat(temp, "/");
    }
    if (partitionNumber > 0) {
        sprintf(temp2, "%d", partitionNumber);
        strcat(temp, temp2);
    }
    printf("Partition command. CD to '%s'\n", temp);
    path->cd(temp);
}

bool IecPartition :: IsValid()
{
    return fm->is_path_valid(path);
}
