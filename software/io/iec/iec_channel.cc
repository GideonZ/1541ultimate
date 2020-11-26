#include "iec_channel.h"
#include "dump_hex.h"

const char *modeNames[] = { "", "Read", "Write", "Replace", "Append", "Relative" };

IecChannel :: IecChannel(IecInterface *intf, int ch)
{
    fm = FileManager :: getFileManager();
    interface = intf;
    channel = ch;
    f = NULL;
    pointer = 0;
    state = e_idle;
    dir_index = 0;
    dir_last = 0;
    last_byte = 0;
    prefetch = 0;
    prefetch_max = 0;
    dirPartition = NULL;
    name.name = NULL;
    name.drive = 0;
    name.extension = ".prg";
    name.mode = e_undefined;
    partition = NULL;
    flags = 0;
    recordSize = 0;
    recordOffset = 0;
    recordDirty = false;
    dir_free = 0;
    pingpong = true;
    swap_buffers();
}

IecChannel :: ~IecChannel()
{
    close_file();
}

void IecChannel :: reset(void)
{
    close_file();

    pingpong = true;
    swap_buffers();
    pointer = 0;
    dir_index = 0;
    dir_last = 0;
    last_byte = 0;
    prefetch = 0;
    prefetch_max = 0;
    dirPartition = NULL;
    partition = NULL;
    name.mode = e_undefined;
}

void IecChannel :: reset_prefetch(void)
{
#if IECDEBUG > 2
    printf("(R)");
#endif
    prefetch = pointer;
}

int IecChannel :: prefetch_data(uint8_t& data)
{
    if (state == e_error) {
        return IEC_NO_FILE;
    }
    if ((last_byte >= 0) && (prefetch >= last_byte)) {
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
    return IEC_BUFFER_END; // prefetch == prefetch_max, buffer needs refresh
}

int IecChannel :: prefetch_more(int max_fetch, uint8_t*& datapointer, int &fetched)
{
    if (state == e_error) {
        fetched = 0;
        return IEC_NO_FILE;
    }
    bool last = false;
    if (last_byte >= 0) { // there is a last byte in this block
        if (prefetch + max_fetch > last_byte) {  // example: prefetch = 0, last_byte = 1 => there are 2 bytes. if max_fetch = 2, it will be set to 2.
            max_fetch = last_byte - prefetch + 1;
            last = true;
        }
    } else {
        if (prefetch + max_fetch > prefetch_max) {
            max_fetch = prefetch_max - prefetch;
        }
    }
    datapointer = &buffer[prefetch];
    fetched = max_fetch;
    prefetch += max_fetch;
    if (last) {
        return IEC_LAST;
    }
    return IEC_OK;
}

int IecChannel :: pop_more(int pop_size)
{
    switch(state) {
        case e_file:
            if(pointer == last_byte) {
                state = e_complete;
                return IEC_NO_FILE; // no more data?
            }
            pointer += pop_size;
            if (pointer == 512) {
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
            }
            pointer += pop_size;
            if(pointer == 32) {
                while(read_dir_entry())
                    ;
                return IEC_OK;
            }
            break;
        case e_record:
            pointer += pop_size;
            if(pointer > last_byte) {
                recordOffset += recordSize;
                if (!read_record(0)) {
                    return IEC_OK;
                } else {
                    state = e_complete;
                    return IEC_NO_FILE;
                }
            }
            break;
        case e_buffer:
            pointer += pop_size;
            break;

        default:
            return IEC_NO_FILE;
    }
    return IEC_OK;
}


int IecChannel :: pop_data(void)
{
    switch(state) {
        case e_file:
            if(pointer == last_byte) {
                state = e_complete;
                return IEC_NO_FILE; // no more data?
            } else if(pointer == 511) {
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
        case e_record:
            if(pointer >= last_byte) {
                recordOffset += recordSize;
                if (!read_record(0)) {
                    return IEC_OK;
                } else {
                    state = e_complete;
                    return IEC_NO_FILE;
                }
            }
            break;
        case e_buffer:
            // do nothing, stay inside of the buffer
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
        if (dir_free > 65535) {
            buffer[2] = 0xFF;
            buffer[3] = 0xFF;
        } else {
            buffer[2] = (uint8_t)(dir_free & 0xFF);
            buffer[3] = (uint8_t)(dir_free >> 8);
        }
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

void IecChannel :: swap_buffers(void)
{
    pingpong = !pingpong;
    curblk = (pingpong) ? &bufblk[1] : &bufblk[0];
    nxtblk = (pingpong) ? &bufblk[0] : &bufblk[1];
    buffer = curblk->bufdata;
}

int IecChannel :: read_block(void)
{
    FRESULT res = FR_DENIED;

    if(f) {
        swap_buffers(); // bring the data in view that was already available
        res = f->read(nxtblk->bufdata, 512, &nxtblk->valid_bytes);
#if IECDEBUG > 2
        printf("Read %d bytes.\n", nxtblk->valid_bytes);
#endif
    }
    if(res != FR_OK) {
        state = e_error;
        return IEC_READ_ERROR;
    }
    if(curblk->valid_bytes == 0)
        state = e_complete; // end should have triggered already

    if(curblk->valid_bytes != 512) {
        last_byte = int(curblk->valid_bytes)-1;
    } else if(nxtblk->valid_bytes == 0) {
        last_byte = 511;
    }
    pointer = 0;
    prefetch = 0;
    prefetch_max = 512;
    return 0;
}

int IecChannel :: read_record(int offset)
{
    FRESULT res = FR_DENIED;
    uint32_t bytes;
    if(f) {
        res = f->read(buffer, recordSize, &bytes);
#if IECDEBUG > 2
        printf("Read record. Expected file position: %d\n", recordOffset);
        dump_hex_relative(buffer, bytes);
#endif
    }
    last_byte = 0; // recordSize - 1;
    for(int i=recordSize-1;i > 0; i--) {
        if (buffer[i] != 0) {
            last_byte = i;
            break;
        }
    }
    if(res != FR_OK) {
        printf("%s!\n", FileSystem :: get_error_string(res));
        state = e_error;
        return IEC_READ_ERROR;
    }
    if(bytes == 0)
        state = e_complete; // end should have triggered already

    pointer = offset;
    prefetch = offset;
    prefetch_max = last_byte + 1;
    recordDirty = false;
    state = e_record;
    return 0;
}

int IecChannel :: write_record(void)
{
    FRESULT res = FR_DENIED;
    uint32_t bytes;

    if (!recordDirty) {
        return IEC_OK; // do nothing; no data was received
    }
    if(f) {
        if ((pointer < recordSize) && (pointer >= 0)) {
            memset(buffer+pointer, 0, recordSize-pointer); // fill up with zeros at the end
            if (pointer == 0) {
                buffer[0] = 0xFF;
            }
        }
        // if everything went well, we are on a record boundary already by the last 'seek' operation
        res = f->seek(recordOffset);
        if (res == FR_OK) {
#if IECDEBUG > 2
            printf("Writing record to position %d:\n", recordOffset);
            dump_hex_relative(buffer, recordSize);
#endif
            res = f->write(buffer, recordSize, &bytes);
            if (res == FR_OK) {
                recordOffset += recordSize; // move to the next record
                res = f->sync();
            }
        }
        // If the file pointer was aligned on a record boundary, it will still be on a record boundary.
        // The file pointer will be on the next record. Should we read it and seek back to the beginning of the record?

        // FIXME: On real drives, the file size does not grow by just writing; only by seeking; so we may want to
        // check the current position against the size and issue "Record does not exist" error when the
        // file boundary would be exceeded here.
    }

    if(res != FR_OK) {
        state = e_error;
        return IEC_WRITE_ERROR;
    }
    return IEC_OK;
}

int IecChannel :: push_data(uint8_t b)
{
    uint32_t bytes;

    switch(state) {
        case e_filename:
            if(pointer < 64)
                buffer[pointer++] = b;
            break;

        case e_record:
            if (pointer == recordSize) {
                // issue error 50
                interface->get_command_channel()->set_error(ERR_OVERFLOW_IN_RECORD, 0, 0);
                return IEC_BYTE_LOST;
            } else {
                buffer[pointer++] = b;
                recordDirty = true;
            }
            break;
            // the actual writing does not happen here, because it is initiated by EOI

        case e_file:
            buffer[pointer++] = b;
            if(pointer == 512) {
                FRESULT res = FR_DENIED;
                if(f) {
                    res = f->write(buffer, 512, &bytes);
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

        case e_buffer:
            if (pointer < 256) {
                buffer[pointer++] = b;
            }
            break;

        default:
            return IEC_BYTE_LOST;
    }
    return IEC_OK;
}

int IecChannel :: push_command(uint8_t b)
{
    switch(b) {
        case 0xF0: // open
            close_file();
            state = e_filename;
            pointer = 0;
            break;
        case 0xE0: // close
            //printf("close %d %d\n", pointer, name.mode);
            if ((name.mode == e_write) || (name.mode == e_append) || (name.mode == e_replace)) {
                if (f) {
                    if (pointer > 0) {
                        uint32_t dummy;
                        FRESULT res = f->write(buffer, pointer, &dummy);
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
            } else if(state == e_record) {
                state = e_idle;
                int r = write_record();
                close_file();
                return r;
            } else {
                close_file();
            }
            state = e_idle;
            break;
        case 0x60:
            reset_prefetch();
            break;
        case 0x00: // end of data
            // if(last_command == 0xF0) // shouldn't we check on state == e_filename here?
            if (state == e_filename) {
                open_file();
            } else if(state == e_record) {
                int r = write_record(); // sets error if necessary. Advances to next record if OK
                if (r == IEC_OK) {
                    r = read_record(0);
                }
                return r;
            }
            break;
        default:
            printf("Error on channel %d. Unknown command: %b\n", channel, b);
    }
    return IEC_OK;
}

void IecChannel :: dump_command(command_t& cmd)
{
#if IECDEBUG
    printf("Command = '%s' on %d.\n", cmd.cmd, cmd.digits);
    printf("Remainder: '%s'\n", cmd.remaining);
#endif
}

void IecChannel :: dump_name(name_t& name, const char *id)
{
#if IECDEBUG
    const char *n = name.name ? name.name : "(null)";
    printf("%s: [%d:] '%s%s'\n  Mode: %s\n  Dir: %s\n  Explicit Extension: %s\n  Record size: %d\n",  id, name.drive, n,
            name.extension, modeNames[name.mode], name.directory ? "Yes" : "No",
            name.explicitExt ? "Yes" : "No", name.recordSize);
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

bool IecChannel :: parse_filename(char *buffer, name_t *name, int default_drive, bool doFlags)
{
#if IECDEBUG
    printf("Parsing filename: '%s' (Default drive: %d, Do flags: %d)\n", buffer, default_drive, doFlags);
#endif

    name->drive = default_drive;
    name->extension = 0;
    name->explicitExt = false;
    name->buffer = false;
    name->mode = (channel == 0) ? e_read : ((channel == 1) ? e_write : e_read);
    name->name = buffer;
    name->directory = false;
    name->recordSize = 0;

    char *s = buffer;
    if (isEmptyString(s)) {
        return false;
    }

    // Handle the case of the $ before the :
    if (*s == '$') {
        name->directory = true;
        s++;
    } else if(*s == '#') {
        name->buffer = true;
        s++;
    } else {
        // Handle the case of the @ before the :
        if (*s == '@') {
            name->mode = e_replace;
            s++;
        }
    }

    name->name = s;
    int len = strlen(s);
    bool hasDigits = false;
    int drive = 0;

    for(int i=0;i<len;i++) {
        if (s[i] == ':') {
            s[i] = 0;
            name->name = s + i + 1;
            // now snoop off the digits and place them into drive number
            for(int j=0;j<i;j++) {
                if (isdigit(s[j])) {
                    drive *= 10;
                    drive += (s[j] - '0');
                    hasDigits = true;
                } else {
                    return false; // only digits are allowed before the colon!
                }
            }
            break;
        }
    }
    if (hasDigits) {
        name->drive = drive;
    }

    char *parts[3] = { NULL, NULL, NULL };
    if (doFlags) {
        split_string(',', name->name, parts, 3);
        for (int i=1;i<3;i++) {
            if (!parts[i]) {
                break;
            }
            switch (toupper(parts[i][0])) {
            case 'P':
                if (!name->explicitExt)
                    name->extension = ".prg";
                name->explicitExt = true;
                break;
            case 'S':
                if (!name->explicitExt)
                    name->extension = ".seq";
                name->explicitExt = true;
                break;
            case 'U':
                if (!name->explicitExt)
                    name->extension = ".usr";
                name->explicitExt = true;
                break;
            case 'L':
                name->extension = ".rel";
                name->explicitExt = true;
                if (parts[3-i]) {
                    name->recordSize = parts[3-i][0];
                }
                name->mode = e_relative;
                i = 2;
                break;
            case 'R':
                name->mode = e_read;
                break;
            case 'W':
                name->mode = e_write;
                break;
            case 'A':
                name->mode = e_append;
                break;
            default:
                return false;
            }
        }
        if (name->name[0] == '@') {
            name->mode = e_replace;
            name->name++;
        }
    }
    if (!name->extension) {
        if (name->mode == e_read) {
            name->extension = ".???";
        } else {
            name->extension = (channel < 2) ? ".prg" : ".seq";
        }
    }

#if IECDEBUG
    printf("Parsing filename successful.\n");
    dump_name(*name, "");
#endif

    return true;
}

int IecChannel :: setup_directory_read(name_t& name)
{
    if (name.explicitExt) {
        dirpattern = &(name.extension[1]); // skip the .
    } else {
        dirpattern = "???";
    }
    const char *first = isEmptyString(name.name)? "*" : name.name;
    dirpattern += first;
    printf("IEC Channel: Opening directory... (Pattern = %s)\n", dirpattern.c_str());
    if (dirPartition->ReadDirectory()) {
        interface->get_command_channel()->set_error(ERR_DRIVE_NOT_READY, dirPartition->GetPartitionNumber());
        state = e_error;
        return -1;
    }

    dirPartition->get_free(dir_free);
    interface->get_command_channel()->set_error(ERR_OK, 0, 0);
    dir_last = dirPartition->GetDirItemCount();
    state = e_dir;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 32;
    last_byte = -1;
    memcpy(buffer, c_header, 32);
    buffer[4] = (uint8_t)dirPartition->GetPartitionNumber();

    // If we are listing a CBM medium, let's use the volume indicator
    FileInfo *header = dirPartition->GetSystemFile(0);
    if ((header->attrib & AM_VOL) && (header->name_format & NAME_FORMAT_CBM)) {
        for(int i=0;i<20;i++) {
            buffer[8+i] = header->lfname[i] & 0x7F;
        }
        buffer[8+16] = 0x22;
    } else {
        const char *fullname = dirPartition->GetFullPath();
        int pos = 8;
        int len = strlen(fullname);
        if (len > 16) {
            fullname += (len - 16);
        }
        while((pos < 24) && (*fullname))
            buffer[pos++] = toupper(*(fullname++));
    }
    dir_index = 0;
    return 0;
}

int IecChannel :: setup_file_access(name_t& name)
{
    flags = FA_READ;

    partition = interface->vfs->GetPartition(name.drive);
    partition->ReadDirectory();
    int pos = partition->FindIecName(name.name, name.extension, false);
    if ((pos < 0) && !name.explicitExt && (name.mode != e_append) && (name.mode != e_write)) {
        name.extension = ".???";
        pos = partition->FindIecName(name.name, name.extension, false);
    }

    FileInfo *info;
    if (pos >= 0) {
        info = partition->GetSystemFile(pos);
        if (strncmp(info->extension, "REL", 3) == 0) {
            name.mode = e_relative;
        }
    }

    switch(name.mode) {
    case e_append:
        if (pos < 0) {
            interface->get_command_channel()->set_error(ERR_FILE_NOT_FOUND, dirPartition->GetPartitionNumber());
            state = e_error;
            return 0;
        }
        info = partition->GetSystemFile(pos);
        strncpy(fs_filename, info->lfname, 64);
        flags = FA_WRITE;
        break;

    case e_write:
        if (pos >= 0) { // file exists, and not allowed to overwrite
            interface->get_command_channel()->set_error(ERR_FILE_EXISTS, dirPartition->GetPartitionNumber());
            state = e_error;
            return 0;
        }
        petscii_to_fat(name.name, fs_filename, 64); // we may convert it back later!!
        //strcpy(fs_filename, name.name);
        strcat(fs_filename, name.extension);
        flags = FA_WRITE|FA_CREATE_NEW|FA_CREATE_ALWAYS;
        break;

    case e_replace:
        petscii_to_fat(name.name, fs_filename, 64);
        //strcpy(fs_filename, name.name);
        strcat(fs_filename, name.extension);
        flags = FA_WRITE|FA_CREATE_ALWAYS;
        break;

    case e_relative:
        petscii_to_fat(name.name, fs_filename, 64);
        strcat(fs_filename, name.extension);
        flags = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
        break;

    default: // read / undefined
        if (pos < 0) {
            interface->get_command_channel()->set_error(ERR_FILE_NOT_FOUND, dirPartition->GetPartitionNumber());
            state = e_error;
            return 0;
        }
        strncpy(fs_filename, info->lfname, 64);
        flags = FA_READ;
    }

    state = e_error; // assume something goes wrong ;)
    return 1; // OK so far
}

int IecChannel :: init_iec_transfer(void)
{
    if (name.mode == e_append) {
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
    prefetch_max = 512;
    state = e_file;

    if(name.mode == e_read) {
        if(f) {
            curblk->valid_bytes = 0;
            // queue up one next block
            f->read(nxtblk->bufdata, 512, &nxtblk->valid_bytes);
#if IECDEBUG > 2
            printf("Initial read %d bytes.\n", nxtblk->valid_bytes);
#endif
        }
        // read block will swap the buffers, so the read above will appear in current block
        return read_block();
    }
    return 0;
}

int IecChannel :: setup_buffer_access(void)
{
    last_byte = 255;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 256;
    state = e_buffer;

    return 0;
}

int IecChannel :: open_file(void)  // name should be in buffer
{
    buffer[pointer] = 0; // string terminator
    printf("Open file. Raw Filename = '%s'\n", buffer);

    fs_filename[0] = 0;
    uint32_t tr = 0;

    parse_filename((char *)buffer, &name, -1, true);

    dirPartition = interface->vfs->GetPartition(name.drive);
    recordSize = 0;

    if(name.directory) {
        return setup_directory_read(name);
    } else if(name.buffer) {
        return setup_buffer_access();
    }

    interface->get_command_channel()->set_error(ERR_OK, 0, 0);
    if (!setup_file_access(name)) {
        interface->get_error_string(fs_filename); // abusing this buffer to store temporary error
        printf("Setup file access failed for file '%d:%s%s': %s\n", name.drive, name.name, name.extension, fs_filename);
        // Something went wrong, so just exit.
        return 0;
    }

    FRESULT fres = fm->fopen(partition->GetPath(), fs_filename, flags | FA_OPEN_FROM_CBM, &f);
    if(f) {
        printf("Successfully opened file %s in %s\n", fs_filename, partition->GetFullPath());

        if (name.mode == e_relative) {
            if (!f->get_size()) { // the file must be newly created, because its size is 0.
                uint16_t wrd = name.recordSize;
                fres = f->write(&wrd, 2, &tr);
                interface->set_error_fres(fres);
                if (fres == FR_OK) {
                    recordSize = name.recordSize;
                }
            } else { // file already exists
                uint16_t wrd;
                fres = f->read(&wrd, 2, &tr);
                recordSize = (uint8_t)wrd;
                if (wrd >= 256) {
                    printf("WARNING: Illegal record size in .rel file...Is it a REL file at all? (%d)\n", wrd);
                }
                interface->set_error_fres(fres);
#if IECDEBUG
                printf("Opened existing relative file. Record size is: %d.\n", recordSize);
#endif
            }
        }
        return init_iec_transfer();
    } else {
        printf("Can't open file %s in %s: %s\n", fs_filename, partition->GetFullPath(), FileSystem :: get_error_string(fres));
        interface->set_error_fres(fres);
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

void IecChannel :: seek_record(const uint8_t *cmd)
{
    const uint32_t c_header = 2;

    // flush
    write_record();

    uint32_t recordNumber = ((uint32_t)cmd[3]) << 8;
    recordNumber |= cmd[2];
    uint8_t offset = cmd[4];

    if (recordNumber != 0)
        recordNumber--;  // Record numbers are starting from 1.
    if (offset != 0)
        offset--; // Offset starts from 1, too.. we are 0 based

#if IECDEBUG > 0
    printf("SEEK Record Number #%d. Offset = %d\n", recordNumber, offset);
#endif
    if (!f) {
        interface->get_command_channel()->set_error(ERR_FILE_NOT_OPEN, 0, 0);
        return;
    }
    if (!recordSize) { // not a (valid) rel file
        interface->get_command_channel()->set_error(ERR_FILE_TYPE_MISMATCH, 0, 0);
        return;
    }
    interface->get_command_channel()->set_error(ERR_OK, 0, 0);

    uint32_t minimumFileSize = c_header + (recordNumber+1) * recordSize; // reserve 2 bytes in the beginning
    uint32_t currentSize = f->get_size();
    FRESULT fres;
    if (currentSize < minimumFileSize) { // append with additional records that are 'FF's, followed by zeros.
        fres = f->seek(currentSize);

        uint8_t *block = new uint8_t[512];
        uint32_t tr = 0;
        memset(block, 0x00, 512);
        uint32_t partialRecord = (currentSize - c_header) % recordSize;

        if (partialRecord) {
            printf("Current file size should be a multiple of record size.. This should not happen.\n");
            fres = f->write(block, partialRecord, &tr);
            currentSize += partialRecord;
        }
#if IECDEBUG > 0
        printf("Append REL file until size %d (was %d)\n", minimumFileSize, currentSize);
#endif
        uint32_t remain = minimumFileSize - currentSize;
        block[0] = 0xFF;
        while(remain >= recordSize) {
            fres = f->write(block, recordSize, &tr);
            remain -= recordSize;
        }
        delete[] block;
        interface->get_command_channel()->set_error(ERR_RECORD_NOT_PRESENT, 0, 0);
    }
    if (offset > recordSize - 1) {
        offset = recordSize - 1;
        interface->get_command_channel()->set_error(ERR_OVERFLOW_IN_RECORD, 0, 0);
    }
    uint32_t targetPosition = c_header + (recordNumber * recordSize); // + offset; // reserve 2 bytes for control (record Size)

    fres = f->seek(targetPosition);
    if (fres != FR_OK) {
        interface->set_error_fres(fres);
        return;
    }
    recordOffset = targetPosition;
    read_record(offset);
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

int IecCommandChannel :: pop_more(int pop_size)
{
    if (state != e_status) {
        return IEC_NO_FILE;
    }
    pointer += pop_size;
    if(pointer > last_byte) {
        state = e_idle;
    }
    return IEC_OK;
}

int IecCommandChannel :: push_data(uint8_t b)
{
    if(wr_pointer < 64) {
        wr_buffer[wr_pointer++] = b;
        return IEC_OK;
    }
    return IEC_BYTE_LOST;
}

bool IecCommandChannel :: parse_command(char *buffer, int length, command_t *command)
{
#if IECDEBUG
    printf("Parsing command: '%s'\n", buffer);
#endif
    // initialize
    bool digits = false;
    command->digits = 0;
    command->remaining = 0;

    // Handle non-null terminated string commands
    if ((buffer[0] == 'P') && ((buffer[1] & 0xF0) == 0x60) && (length >=4)) { // rel file positioning command
        memcpy(command->cmd, buffer, 5);
        if (length < 5) {
            command->cmd[4] = 1; // default for Positioning command
        }
        return true;
    }

    // First strip off any carriage returns, in any case
    int len = strlen(buffer);
    while ((len > 0) && (buffer[len-1] == 0x0d)) {
        len--;
        buffer[len] = 0;
    }

    int cmdlen = 0;
    int i = 0;
    // First analyze the command itself, if given that it is a command

    for(i=0;(i<len) && (i < 15);i++) {
        if (isalpha(buffer[i]) || (buffer[i] == '-')) {
            command->cmd[i] = toupper(buffer[i]);
            cmdlen++;
        } else {
            break;
        }
    }
    command->cmd[cmdlen] = 0; // null terminate string

    while(isdigit(buffer[i])) {
        digits = true;
        command->digits *= 10;
        command->digits += (buffer[i] - '0');
        i++;
    }
    if (!digits) {
        command->digits = -1;
    }
    command->remaining = & buffer[i];
    if (buffer[i] == 0) {
        return true;
    }
    if (buffer[i] != ':') {
        return false;
    }
    i++;
    command->remaining++;
    if (buffer[i] == 0) {
        return true;
    }
    return true;
}

void IecCommandChannel :: exec_command(command_t &command)
{
    IecPartition *p;
    IecChannel *ch;

    if (isEmptyString(command.cmd)) {
        set_error(ERR_SYNTAX_ERROR_GEN);
    } else if(command.cmd[0] == 'P') {
        if ((command.cmd[1] <= 0x60) || (command.cmd[1] >= 0x6E)) {
            set_error(ERR_SYNTAX_ERROR_CMD);
        }
        ch = interface->get_data_channel(command.cmd[1] & 0x0F);
        ch->seek_record((uint8_t *)command.cmd);
    } else if (strncmp(command.cmd, "B-", 2) == 0) { // Block comand
        block_command(command);
    } else if (strcmp(command.cmd, "U") == 0) { // Block Read or Block write
        block_command(command);
    } else if (strcmp(command.cmd, "CD") == 0) {
        p = interface->vfs->GetPartition(command.digits);
        if (isEmptyString(command.remaining)) {
            set_error(ERR_SYNTAX_ERROR_CMD);
        } else if (p->cd(command.remaining)) {
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
        } else if (isEmptyString(command.remaining)) {
            set_error(ERR_SYNTAX_ERROR_CMD);
        } else if (hasIllegalChars(command.remaining)) {
            set_error(ERR_SYNTAX_ERROR_CMD);
        } else if (p->MakeDirectory(command.remaining) == FR_OK) {
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

void IecCommandChannel :: block_command(command_t& cmd)
{
    IecChannel *channel;
    IecPartition *partition;
    FRESULT fres;
    int operation = 0;

    int ch, dr, tr, sc;
    if (cmd.cmd[0] == 'U') {
        if ((cmd.digits < 1) || (cmd.digits > 2)) {
            set_error(ERR_SYNTAX_ERROR_CMD);
            return;
        }
        operation = cmd.digits;
    } else { // must be B-
        int n = sscanf(cmd.remaining, "%d%d%d%d", &ch, &dr, &tr, &sc);
        switch (cmd.cmd[2]) {
        case 'R':
            operation = 3;
            break;
        case 'W':
            operation = 4;
            break;
        case 'P':
            if ((n != 2) || (ch < 0) || (ch > 14)) { // illegal
                set_error(ERR_SYNTAX_ERROR_CMD);
                return;
            }
            if ((dr < 0) || (dr > 255)) {
                set_error(ERR_SYNTAX_ERROR_CMD);
                return;
            }
            channel = interface->get_data_channel(ch);
            channel->pointer = dr;
            channel->reset_prefetch();
            set_error(ERR_OK);
            return;
        default:
            set_error(ERR_SYNTAX_ERROR_CMD);
            return;
        }
    }

    // when we get here, it's either U1, U2, B-R or B-W, which all take 4 params

    int n = sscanf(cmd.remaining, "%d%d%d%d", &ch, &dr, &tr, &sc);
    if ((n != 4) || (ch < 0) || (ch > 14)) { // illegal
        set_error(ERR_SYNTAX_ERROR_CMD);
        return;
    }
    if (dr >= MAX_PARTITIONS) {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return;
    }
    channel = interface->get_data_channel(ch);
    partition = interface->vfs->GetPartition(dr);
    FileSystem *fs = partition->GetFileSystem();
    if (!fs || !(fs->supports_direct_sector_access())) {
        set_error(ERR_DRIVE_NOT_READY);
        return;
    }
    switch(operation) {
    case 1:
    case 3:
#if IECDEBUG
        printf("Read Track %d Sector %d of Drive %d into buffer of channel %d.\n", tr, sc, dr, ch);
#endif
        fres = fs->read_sector(channel->buffer, tr, sc);
        interface->set_error_fres(fres);
        state = e_idle;
        break;
    case 2:
    case 4:
#if IECDEBUG
        printf("Write Track %d Sector %d of Drive %d from buffer of channel %d.\n", tr, sc, dr, ch);
#endif
        fres = fs->write_sector(channel->buffer, tr, sc);
        interface->set_error_fres(fres);
        state = e_idle;
        break;
    }
}

void IecCommandChannel :: renam(command_t& cmd)
{
    char fromName[32];
    int  fromPart;
    char toName[32];
    int  toPart;

    bool dummy;
    int fromAt;

    char *leftright[2] = { 0, 0 };

    name_t destination;
    name_t source;

    split_string('=', cmd.remaining, leftright, 2);
    if (leftright[1]) {
        parse_filename(leftright[0], &destination, cmd.digits, true);
        parse_filename(leftright[1], &source, cmd.digits, true);
    } else {
        // there is no = token
        set_error(ERR_SYNTAX_ERROR_CMD);
        return;
    }

    strcpy(toName, destination.name);
    toPart = destination.drive;

    const char *fromExt = ".???";
    strcpy(fromName, source.name);
    fromPart = source.drive;

    // does the source name have an extension specified?
    if (source.explicitExt) {
        fromExt = source.extension;
    }

    printf("From: %d:%s%s To: %d:%s%s \n", fromPart, fromName, fromExt, toPart, toName, destination.extension);

    IecPartition *partitionFrom = interface->vfs->GetPartition(fromPart);
    partitionFrom->ReadDirectory();
    int posFrom = partitionFrom->FindIecName(fromName, fromExt, true);
    if (posFrom < 0) {
        set_error(ERR_FILE_NOT_FOUND, 0);
        return;
    }
    FileInfo *fromInfo = partitionFrom->GetSystemFile(posFrom);
    // Yey, we finally got the actual file we need to rename!!

    if ((!destination.explicitExt) && !(fromInfo->attrib & AM_DIR)) {
        add_extension(toName, fromInfo->extension, 32);
    } else {
        add_extension(toName, destination.extension, 32);
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

void IecCommandChannel :: copy(command_t& cmd)
{
    // parse remainder
    char *leftright[2] = { 0, 0 };
    char *files[4] = { 0, 0, 0, 0 };

    name_t destination;

    split_string('=', cmd.remaining, leftright, 2);
    if (leftright[1]) {
        split_string(',', leftright[1], files, 4);
        parse_filename(leftright[0], &destination, cmd.digits, true);
    } else {
        // there is no = token
        set_error(ERR_SYNTAX_ERROR_CMD);
        return;
    }


    FileInfo *infos[4] = { 0, 0, 0, 0 };
    name_t names[4];

    // First try to find all (possibly) four sources
    for(int i=0;i<4;i++) {
        if (!files[i]) {
            break;
        }
        parse_filename(files[i], &names[i], cmd.digits, false);
        if ((names[i].directory || names[i].mode == e_replace)) {
            set_error(ERR_SYNTAX_ERROR_CMD, i+1);
            return;
        }

        IecPartition *partitionFrom = interface->vfs->GetPartition(names[i].drive);
        partitionFrom->ReadDirectory();
        int posFrom = partitionFrom->FindIecName(names[i].name, "???", false);
        if (posFrom < 0) {
            set_error(ERR_FILE_NOT_FOUND, i);
            return;
        }
        FileInfo *info = partitionFrom->GetSystemFile(posFrom);
#if IECDEBUG
        printf("Found matching file %d: %s -> %s\n", i, files[i], info->lfname);
#endif
        infos[i] = info;
        if (info->attrib & AM_DIR) {
            set_error(ERR_FILE_TYPE_MISMATCH, i);
            return;
        }
    }

    // Now try to create the output file
    IecPartition *partitionTo = interface->vfs->GetPartition(destination.drive);
    char toName[32];
    strcpy(toName, destination.name);
    // Take the extension from the first file to copy
    if (destination.explicitExt) {
        add_extension(toName, destination.extension, 32);
    } else {
        add_extension(toName, infos[0]->extension, 32);
    }

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
            IecPartition *partitionFrom = interface->vfs->GetPartition(names[i].drive);
            File *fi;
            FRESULT fres = fm->fopen(partitionFrom->GetPath(), infos[i]->lfname, FA_READ, &fi);
            if (fres != FR_OK) {
                fm->fclose(fo);
                set_error(ERR_FRESULT_CODE, fres);
                return;
            }
            if (fi) {
#if IECDEBUG
                printf("Opened '%s' for copy.\n", infos[i]->lfname);
#endif
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
                parse_command((char *)wr_buffer, wr_pointer, &command);
                dump_command(command);
                exec_command(command);
            }
            wr_pointer = 0;
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
