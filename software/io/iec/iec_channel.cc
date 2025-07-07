#include "iec_channel.h"
#include "dump_hex.h"
#include "rtc.h"

IecChannel::IecChannel(IecDrive *dr, int ch)
{
    fm = FileManager::getFileManager();
    drive = dr;
    channel = ch;
    f = NULL;
    dir = NULL;
    part_idx = 0;
    pointer = 0;
    state = e_idle;
    last_byte = 0;
    prefetch = 0;
    prefetch_max = 0;
    partition = NULL;
    flags = 0;
    recordSize = 0;
    recordOffset = 0;
    recordDirty = false;
    dir_free = 0;
    pingpong = true;
    swap_buffers();
}

IecChannel::~IecChannel()
{
    close_file();
}

void IecChannel::reset(void)
{
    close_file();

    pingpong = true;
    swap_buffers();
    pointer = 0;
    last_byte = 0;
    prefetch = 0;
    prefetch_max = 0;
    partition = NULL;
}

void IecChannel::reset_prefetch(void)
{
#if IECDEBUG > 2
    printf("(R%d)", channel);
#endif
    prefetch = pointer;
}

t_channel_retval IecChannel::prefetch_data(uint8_t& data)
{
    if (state == e_error) {
        return IEC_NO_FILE;
    }

    // When reading from the last block, last_byte is set to a value >= 0.
    // Three cases; not the last byte -> IEC_OK; the last byte -> IEC_LAST, beyond the last byte -> IEC_NO_FILE
    if (last_byte >= 0) {
        t_channel_retval ret = IEC_OK;
        if (prefetch == last_byte) {
            ret = IEC_LAST;
            data = buffer[prefetch];
        } else if (prefetch > last_byte) {
            ret = IEC_NO_FILE;
        } else {
            data = buffer[prefetch];
        }
        prefetch++;
        return ret;
    }

    // When reading not from the last block, last byte is set to -1; prefetching goes all the way up to prefetch max
    // which is likely set to 512.

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

t_channel_retval IecChannel::prefetch_more(int max_fetch, uint8_t*& datapointer, int &fetched)
{
    if (state == e_error) {
        fetched = 0;
        return IEC_NO_FILE;
    }
    bool last = false;
    if (last_byte >= 0) { // there is a last byte in this block
        if (prefetch + max_fetch > last_byte) { // example: prefetch = 0, last_byte = 1 => there are 2 bytes. if max_fetch = 2, it will be set to 2.
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

t_channel_retval IecChannel::pop_more(int pop_size)
{
    switch (state) {
    case e_file:
        if (pointer == last_byte) {
            state = e_complete;
            return IEC_NO_FILE; // no more data?
        }
        pointer += pop_size;
        if (pointer == 512) {
            if (read_block())  // also resets pointer.
                return IEC_READ_ERROR;
            else
                return IEC_OK;
        }
        break;
    case e_dir:
    case e_partlist:
        if (pointer == last_byte) {
            state = e_complete;
            return IEC_NO_FILE; // no more data?
        }
        pointer += pop_size;
        if (pointer == prefetch_max) {
            while (read_dir_entry() > 0)
                ;
            return IEC_OK;
        }
        break;
    case e_record:
        pointer += pop_size;
        if (pointer > last_byte) {
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

t_channel_retval IecChannel::pop_data(void)
{
    switch (state) {
    case e_file:
        if (pointer == last_byte) {
            state = e_complete;
            pointer ++; // make sure it's beyond the last byte now
            return IEC_NO_FILE; // no more data?
        } else if (pointer == 511) {
            if (read_block())  // also resets pointer.
                return IEC_READ_ERROR;
            else
                return IEC_OK;
        }
        break;
    case e_dir:
    case e_partlist:
        if (pointer == last_byte) {
            state = e_complete;
            return IEC_NO_FILE; // no more data?
        } else if (pointer == prefetch_max - 1) {
            while (read_dir_entry() > 0)
                ;
            return IEC_OK;
        }
        break;
    case e_record:
        if (pointer >= last_byte) {
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

void IecChannel::swap_buffers(void)
{
    pingpong = !pingpong;
    curblk = (pingpong) ? &bufblk[1] : &bufblk[0];
    nxtblk = (pingpong) ? &bufblk[0] : &bufblk[1];
    buffer = curblk->bufdata;
}

int IecChannel::read_block(void)
{
    FRESULT res = FR_DENIED;

    if (f) {
        swap_buffers(); // bring the data in view that was already available
        res = f->read(nxtblk->bufdata, 512, &nxtblk->valid_bytes);
#if IECDEBUG > 2
        printf("Read %d bytes.\n", nxtblk->valid_bytes);
#endif
    }
    if (res != FR_OK) {
        state = e_error;
        return IEC_READ_ERROR;
    }
    if (curblk->valid_bytes == 0)
        state = e_complete; // end should have triggered already

    if (curblk->valid_bytes != 512) {
        last_byte = int(curblk->valid_bytes) - 1;
    } else if (nxtblk->valid_bytes == 0) {
        last_byte = 511;
    }
    pointer = 0;
    prefetch = 0;
    prefetch_max = 512;
    return 0;
}

t_channel_retval IecChannel::push_data(uint8_t b)
{
    uint32_t bytes;

    switch (state) {
    case e_filename:
        if (pointer < 64)
            buffer[pointer++] = b;
        break;

    case e_record:
        if (pointer == recordSize) {
            // issue error 50
            drive->get_command_channel()->set_error(ERR_OVERFLOW_IN_RECORD, 0, 0);
            return IEC_BYTE_LOST;
        } else {
            buffer[pointer++] = b;
            recordDirty = true;
        }
        break;
        // the actual writing does not happen here, because it is initiated by EOI

    case e_file:
        buffer[pointer++] = b;
        if (pointer == 512) {
            FRESULT res = FR_DENIED;
            if (f) {
                res = f->write(buffer, 512, &bytes);
            }
            if (res != FR_OK) {
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

t_channel_retval IecChannel::push_command(uint8_t b)
{
    switch (b) {
    case 0xF0: // open
        close_file();
        state = e_filename;
        pointer = 0;
        break;
    case 0xE0: // close
        //printf("close %d %d\n", pointer, name.mode);
        if ((filemode == e_write) || (filemode == e_append)) {
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
        } else if (state == e_record) {
            state = e_idle;
            t_channel_retval r = write_record();
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
        if (state == e_filename) {
            open_file();
        } else if (state == e_record) {
            t_channel_retval r = write_record(); // sets error if necessary. Advances to next record if OK
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

t_channel_retval IecChannel::read_record(int offset)
{
    FRESULT res = FR_DENIED;
    uint32_t bytes;
    if (f) {
        res = f->read(buffer, recordSize, &bytes);
#if IECDEBUG > 2
        printf("Read record. Expected file position: %d\n", recordOffset);
        dump_hex_relative(buffer, bytes);
#endif
    }
    last_byte = 0; // recordSize - 1;
    for (int i = recordSize - 1; i > 0; i--) {
        if (buffer[i] != 0) {
            last_byte = i;
            break;
        }
    }
    if (res != FR_OK) {
        printf("%s!\n", FileSystem::get_error_string(res));
        state = e_error;
        return IEC_READ_ERROR;
    }
    if (bytes == 0)
        state = e_complete; // end should have triggered already

    pointer = offset;
    prefetch = offset;
    prefetch_max = last_byte + 1;
    recordDirty = false;
    state = e_record;
    return IEC_OK;
}

t_channel_retval IecChannel::write_record(void)
{
    FRESULT res = FR_DENIED;
    uint32_t bytes;

    if (!recordDirty) {
        return IEC_OK; // do nothing; no data was received
    }
    if (f) {
        if ((pointer < recordSize) && (pointer >= 0)) {
            memset(buffer + pointer, 0, recordSize - pointer); // fill up with zeros at the end
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

    if (res != FR_OK) {
        state = e_error;
        return IEC_WRITE_ERROR;
    }
    return IEC_OK;
}


int IecChannel::read_dir_entry(void)
{
    FileInfo info(40);
    FRESULT fres;
    if (state == e_dir) {
        if (!dir) {
            return -1;
        }
        fres = dir->get_entry(info);
    } else {
        // list partition and construct fake info
        if (part_idx >= MAX_PARTITIONS) {
            fres = FR_NO_FILE;
        } else {
            fres = FR_OK;
            IecPartition *prt = drive->vfs->GetPartition(part_idx);
            if (!prt) {
                part_idx ++;
                return 1;
            }
            info.size = part_idx * 254;
            uint32_t fattime = get_fattime();
            info.date = fattime >> 16;
            info.time = fattime & 0xFFFF;
            info.attrib = AM_DIR;
            const char *ps = prt->GetRootPath();
            //sprintf(info.lfname, "PART%03d", part_idx);
            strncpy(info.lfname, ps, info.lfsize);
            part_idx ++;
        }
    }

    if (fres != FR_OK) {
        if (dir) {
            delete dir;
            dir = NULL;
        }

        if (dir_free > 65535) {
            buffer[2] = 0xFF;
            buffer[3] = 0xFF;
        } else {
            buffer[2] = (uint8_t) (dir_free & 0xFF);
            buffer[3] = (uint8_t) (dir_free >> 8);
        }
        memcpy(&buffer[4], "BLOCKS FREE.             \0\0\0", 28);
        last_byte = 31;
        pointer = 0;
        prefetch_max = 31;
        prefetch = 0;
        return 0;
    }
        
    // convert FAT name to CBM name
    char cbm_name[24];
    filetype_t ftype = e_any;
    IecPartition::CreateIecName(&info, cbm_name, ftype);

    // filter by type
    if (name_to_open.dir_opt.filetypes) {
        if ((name_to_open.dir_opt.filetypes & (1 << (int)ftype)) == 0) {
            return 1;
        }
    }
    // filter by pattern
    if (name_to_open.file.filename.length() > 0) {
        if (!pattern_match(name_to_open.file.filename.c_str(), cbm_name, false)) {
            return 1;
        }
    }

    // filter by date and time
    uint32_t dt = (((uint32_t)info.date) << 16) | info.time;
    if (name_to_open.dir_opt.max_datetime) {
        if (dt > name_to_open.dir_opt.max_datetime) {
            return 1;
        }
    }
    if (name_to_open.dir_opt.min_datetime) {
        if (dt < name_to_open.dir_opt.min_datetime) {
            return 1;
        }
    }

    uint32_t size = 0;
    uint32_t size2 = 0;
    size = info.size;
    size = (size + 253) / 254;
    if (size > 9999)
        size = 9999;
    size2 = size;
    int chars = 1;
    while (size2 >= 10) {
        size2 /= 10;
        chars++;
    }
    int spaces = 3 - chars;
    buffer[2] = size & 255;
    buffer[3] = size >> 8;
    int pos = 4;
    while ((spaces--) >= 0)
        buffer[pos++] = 32;
    buffer[pos++] = 34;
    const char *src = cbm_name;
    while (*src)
        buffer[pos++] = *(src++);
    buffer[pos++] = 34;
    while (pos < 32)
        buffer[pos++] = 32;

    const char *types[] = { "ANY", "PRG", "SEQ", "USR", "REL", "DIR" };
    memcpy(&buffer[27 - chars], types[(int)ftype], 3);

    pointer = 0;
    prefetch = 0;
    switch(name_to_open.dir_opt.timefmt) {
    case e_stamp_none:
        buffer[31] = 0;
        prefetch_max = 32;
        break;
    case e_stamp_long:
        cbmdos_time(dt, (char *)buffer + 31 - chars, true);
        prefetch_max = 31 + 18 - chars;
        break;
    case e_stamp_short:
        buffer[28 - chars] = 32; // space out second letter of file type
        cbmdos_time(dt, (char *)buffer + 29 - chars, false);
        prefetch_max = 29 + 14 - chars;
        break;
    }
    return 0;
}

/*
    if (!name->extension) {
        if (name->mode == e_read) {
            name->extension = ".???";
        } else {
            name->extension = (channel < 2) ? ".prg" : ".seq";
        }
    }
*/
int IecChannel :: setup_partition_read()
{
    printf("Setup partition read\n");
    drive->get_command_channel()->set_error(ERR_ALL_OK, 0, 0);
    state = e_partlist;
    part_idx = 0;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 32;
    last_byte = -1;
    memcpy(buffer, c_header, 32);
    memcpy(buffer+8, "ULTIMATE HD", 11);
    return 0;
}

int IecChannel :: setup_directory_read()
{
    printf("Setup dir read\n");
    char full_path[256];
    char file_name[128];

    // previously not closed??
    if (dir) {
        delete dir;
    }    
    if (!drive->vfs->construct_path(name_to_open, full_path, 256, file_name, 128)) {
        drive->get_command_channel()->set_error(ERR_PARTITION_ERROR, name_to_open.file.partition);
        state = e_error;
        return -1;
    } 
    printf("Full Path = %s, filename = %s\n", full_path, file_name);

    FRESULT fres = fm->open_directory(full_path, &dir);
    if (fres != FR_OK) {
        printf("opening dir failed %s\n", FileSystem::get_error_string(fres));
        drive->get_command_channel()->set_error(ERR_DRIVE_NOT_READY, name_to_open.file.partition);
        state = e_error;
        return -1;
    }

    Path path(full_path);
    uint32_t cluster_size;
    fm->get_free(&path, dir_free, cluster_size);
    drive->get_command_channel()->set_error(ERR_ALL_OK, 0, 0);
    state = e_dir;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 32;
    last_byte = -1;
    memcpy(buffer, c_header, 32);
    buffer[4] = (uint8_t) name_to_open.file.partition;

    IecPartition *part = drive->vfs->GetPartition(name_to_open.file.partition);

    // If we are listing a CBM medium, let's use the volume indicator
//    FileInfo *header = dirPartition->GetSystemFile(0);
//    if ((header->attrib & AM_VOL) && (header->name_format & NAME_FORMAT_CBM)) {
//        for (int i = 0; i < 20; i++) {
//            buffer[8 + i] = header->lfname[i] & 0x7F;
//        }
//        buffer[8 + 16] = 0x22;
//    } else {
        const char *fullname = part->GetRelativePath();
        int pos = 8;
        int len = strlen(fullname);
        if (len > 16) {
            fullname += (len - 16);
        }
        while ((pos < 24) && (*fullname))
            buffer[pos++] = toupper(*(fullname++));
//    }


//    if (dir) {
//        delete dir;
//    }
    return 0;
}

int IecChannel :: setup_file_access()
{
    return 0;
}

/*
int IecChannel::setup_directory_read(name_t& name)
{
    if (name.explicitExt) {
        dirpattern = &(name.extension[1]); // skip the .
    } else {
        dirpattern = "???";
    }
    const char *first = isEmptyString(name.name) ? "*" : name.name;
    dirpattern += first;
    printf("IEC Channel: Opening directory... (Pattern = %s)\n", dirpattern.c_str());
    if (dirPartition->ReadDirectory()) {
        drive->get_command_channel()->set_error(ERR_DRIVE_NOT_READY, dirPartition->GetPartitionNumber());
        state = e_error;
        return -1;
    }

    dir_index = 0;
    return 0;
}

int IecChannel::setup_file_access(name_t& name)
{
    drive->get_command_channel()->set_error(ERR_ALL_OK, 0, 0);
    if (!setup_file_access(name)) {
        drive->get_error_string(fs_filename); // abusing this buffer to store temporary error
        printf("Setup file access failed for file '%d:%s%s': %s\n", name.drive, name.name, name.extension, fs_filename);
        // Something went wrong, so just exit.
        return 0;
    }

    FRESULT fres = fm->fopen(partition->GetPath(), fs_filename, flags | FA_OPEN_FROM_CBM, &f);
    if (f) {
        printf("Successfully opened file %s in %s\n", fs_filename, partition->GetFullPath());

        if (name.mode == e_relative) {
            if (!f->get_size()) { // the file must be newly created, because its size is 0.
                uint16_t wrd = name.recordSize;
                fres = f->write(&wrd, 2, &tr);
                drive->set_error_fres(fres);
                if (fres == FR_OK) {
                    recordSize = name.recordSize;
                }
            } else { // file already exists
                uint16_t wrd;
                fres = f->read(&wrd, 2, &tr);
                recordSize = (uint8_t) wrd;
                if (wrd >= 256) {
                    printf("WARNING: Illegal record size in .rel file...Is it a REL file at all? (%d)\n", wrd);
                }
                drive->set_error_fres(fres);
#if IECDEBUG
                printf("Opened existing relative file. Record size is: %d.\n", recordSize);
#endif
            }
        }
        return init_iec_transfer();
    } else {
        printf("Can't open file %s in %s: %s\n", fs_filename, partition->GetFullPath(), FileSystem::get_error_string(fres));
        drive->set_error_fres(fres);
    }
    return 0;

    flags = FA_READ;

    partition = drive->vfs->GetPartition(name.drive);
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

    switch (name.mode) {
    case e_append:
        if (pos < 0) {
            drive->get_command_channel()->set_error(ERR_FILE_NOT_FOUND, dirPartition->GetPartitionNumber());
            state = e_error;
            return 0;
        }
        info = partition->GetSystemFile(pos);
        strncpy(fs_filename, info->lfname, 64);
        flags = FA_WRITE;
        break;

    case e_write:
        if (pos >= 0) { // file exists, and not allowed to overwrite
            drive->get_command_channel()->set_error(ERR_FILE_EXISTS, dirPartition->GetPartitionNumber());
            state = e_error;
            return 0;
        }
        petscii_to_fat(name.name, fs_filename, 64); // we may convert it back later!!
        //strcpy(fs_filename, name.name);
        strcat(fs_filename, name.extension);
        flags = FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS;
        break;

    case e_replace:
        petscii_to_fat(name.name, fs_filename, 64);
        //strcpy(fs_filename, name.name);
        strcat(fs_filename, name.extension);
        flags = FA_WRITE | FA_CREATE_ALWAYS;
        break;

    case e_relative:
        petscii_to_fat(name.name, fs_filename, 64);
        strcat(fs_filename, name.extension);
        flags = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
        break;

    default: // read / undefined
        if (pos < 0) {
            drive->get_command_channel()->set_error(ERR_FILE_NOT_FOUND, dirPartition->GetPartitionNumber());
            state = e_error;
            return 0;
        }
        strncpy(fs_filename, info->lfname, 64);
        flags = FA_READ;
    }

    state = e_error; // assume something goes wrong ;)
    return 1; // OK so far
}
*/
int IecChannel::init_iec_transfer(void)
{
    if (filemode == e_append) {
        FRESULT fres = f->seek(f->get_size());
        if (fres != FR_OK) {
            drive->get_command_channel()->set_error(ERR_FRESULT_CODE, fres);
            state = e_error;
            return 0;
        }
    }
    last_byte = -1;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 512;
    state = e_file;

    if (filemode == e_read) {
        if (f) {
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

int IecChannel::setup_buffer_access(void)
{
    last_byte = 255;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 256;
    state = e_buffer;

    return 0;
}

int IecChannel::open_file(void)  // name should be in buffer
{
    buffer[pointer] = 0; // string terminator
    printf("Open file. Raw Filename = '%s'\n", buffer);
    parse_open((const char *)buffer, name_to_open);

    IecPartition *partition = drive->vfs->GetPartition(name_to_open.file.partition);
    recordSize = 0;

    switch(name_to_open.dir_opt.stream) {
    case e_stream_buffer:
        return setup_buffer_access();
    case e_stream_dir:
        return setup_directory_read();
    case e_stream_partitions:
        return setup_partition_read();
    case e_stream_file:
        return setup_file_access();
    default: // file
        printf("Unknown stream mode\n");
    }
    return -1;
}

int IecChannel::close_file(void) // file should be open
{
    if (f)
        fm->fclose(f);
    f = NULL;
    state = e_idle;
    return 0;
}

int IecChannel::ext_open_file(const char *name)
{
    strncpy((char *) buffer, name, 255);
    buffer[255] = 0;
    pointer = strlen((char *) buffer);

    int result = open_file();
    if (state == e_dir || state == e_file) {
        return 1;
    }
    return 0;
}

int IecChannel::ext_close_file(void)
{
    return push_command(0xE0);
}

void IecChannel::seek_record(const uint8_t *cmd)
{
    const uint32_t c_header = 2;

    // flush
    write_record();

    uint32_t recordNumber = ((uint32_t) cmd[3]) << 8;
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
        drive->get_command_channel()->set_error(ERR_FILE_NOT_OPEN, 0, 0);
        return;
    }
    if (!recordSize) { // not a (valid) rel file
        drive->get_command_channel()->set_error(ERR_FILE_TYPE_MISMATCH, 0, 0);
        return;
    }
    drive->get_command_channel()->set_error(ERR_ALL_OK, 0, 0);

    uint32_t minimumFileSize = c_header + (recordNumber + 1) * recordSize; // reserve 2 bytes in the beginning
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
        while (remain >= recordSize) {
            fres = f->write(block, recordSize, &tr);
            remain -= recordSize;
        }
        delete[] block;
        drive->get_command_channel()->set_error(ERR_RECORD_NOT_PRESENT, 0, 0);
    }
    if (offset > recordSize - 1) {
        offset = recordSize - 1;
        drive->get_command_channel()->set_error(ERR_OVERFLOW_IN_RECORD, 0, 0);
    }
    uint32_t targetPosition = c_header + (recordNumber * recordSize); // + offset; // reserve 2 bytes for control (record Size)

    fres = f->seek(targetPosition);
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
        return;
    }
    recordOffset = targetPosition;
    read_record(offset);
}

/******************************************************************************
 * Command Channel
 ******************************************************************************/

IecCommandChannel::IecCommandChannel(IecDrive *dr, int ch) :
        IecChannel(dr, ch)
{
    parser = new IecParser(this);
    wr_pointer = 0;
}

IecCommandChannel::~IecCommandChannel()
{

}

void IecCommandChannel::reset(void)
{
    IecChannel::reset();
    set_error(ERR_DOS);
}

void IecCommandChannel::set_error(int err, int track, int sector)
{
    if (err >= 0) {
        drive->set_error(err, track, sector);
        state = e_idle;
    }
}

void IecCommandChannel::get_error_string(void)
{
    last_byte = drive->get_error_string((char *) buffer) - 1;
    //printf("Get last error: last = %d. buffer = %s.\n", last_byte, buffer);
    pointer = 0;
    prefetch = 0;
    prefetch_max = last_byte;

    drive->set_error(0, 0, 0);
}

void IecCommandChannel::talk(void)
{
    if (state != e_status) {
        get_error_string();
        state = e_status;
    }
}

t_channel_retval IecCommandChannel::pop_data(void)
{
    if (state == e_status) {
        if (pointer > last_byte) {
            state = e_idle;
            return IEC_NO_FILE;
        }
        if (pointer == last_byte) {
            state = e_idle;
            return IEC_LAST;
        }
        pointer++;
        return IEC_OK;
    }
    return IEC_NO_FILE;
}

t_channel_retval IecCommandChannel::pop_more(int pop_size)
{
    if (state != e_status) {
        return IEC_NO_FILE;
    }
    pointer += pop_size;
    if (pointer > last_byte) {
        state = e_idle;
    }
    return IEC_OK;
}

t_channel_retval IecCommandChannel::push_data(uint8_t b)
{
    if (wr_pointer < 64) {
        wr_buffer[wr_pointer++] = b;
        return IEC_OK;
    }
    return IEC_BYTE_LOST;
}

void print_file(filename_t& file)
{
    printf("P=%d, Path='%s', Filename='%s', Wildcard: %s\n",
        file.partition, file.path.c_str(), file.filename.c_str(), file.has_wildcard?"true":"false" );
}

#define GETPARTITION(part, partition, reterr) \
    IecPartition *partition = drive->vfs->GetPartition(part); \
    if (!partition) { \
        drive->get_command_channel()->set_error(ERR_PARTITION_ERROR, drive->vfs->GetTargetPartitionNumber(part)); \
        state = e_error; \
        return reterr; \
    }

static void iec_path_to_fs_path(mstring &path)
{
    // Path conversion needs to take place, because
    // in this context, _ means "..", and when the path starts
    // with a /, it should be stripped off, while // means root,
    // which corresponds to starting with / in Ultimate VFS.
    if (path[0] == '/' && path[1] != '/')
        path = path.c_str() + 1;
    path.replace("_", "..");
    path.replace("//", "/");
//    if (path[-1] == '/')
//        path.set(-1, 0);
}

const char *IecChannel :: ConstructPath(mstring& work, filename_t& name, filetype_t ftype, fileaccess_t acc)
{
    const char *types[] = { ".???", ".prg", ".seq", ".usr", ".rel", "" };

    GETPARTITION(name.partition, partition, NULL);
    // first get names in more usable format
    iec_path_to_fs_path(name.path);
    char fatname[48];
    petscii_to_fat(name.filename.c_str(), fatname, 48);

    if (ftype == e_any) {
        if ((acc == e_write) || (acc == e_append)) {
            if (channel == 0) {
                ftype = e_prg;
            } else {
                ftype = e_seq;
            }
        }
    }

    const char *ext = types[(int)ftype];
    if (*ext)
        set_extension(fatname, ext, 48);

    Path workpath;
    workpath.cd(partition->GetRelativePath());
    if(!workpath.cd(name.path.c_str())) return NULL;
    if(!workpath.cd(fatname)) return NULL;
    const char *pp = workpath.get_path();
    work = partition->GetRootPath();
    work += (pp + 1);
    return work.c_str();
}


int IecCommandChannel :: do_block_read(int chan, int part, int track, int sector)
{
    if ((chan < 0) || (chan > 14)) {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return ERR_SYNTAX_ERROR_CMD;
    }
    if (part >= MAX_PARTITIONS) {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return ERR_SYNTAX_ERROR_CMD;
    }
    IecChannel *channel = iec_drive->get_data_channel(chan);
    GETPARTITION(part, partition, -1);
    Path path(partition->GetFullPath());
    FRESULT fres;
    fres = fm->fs_read_sector(&path, channel->buffer, track, sector);
    drive->set_error_fres(fres);
    channel->pointer = 0;
    channel->reset_prefetch();
    state = e_idle;
    return 0;
}

int IecCommandChannel::do_block_write(int chan, int part, int track, int sector)
{
    if ((chan < 0) || (chan > 14)) {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return ERR_SYNTAX_ERROR_CMD;
    }
    if (part >= MAX_PARTITIONS) {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return ERR_SYNTAX_ERROR_CMD;
    }
    IecChannel *channel = iec_drive->get_data_channel(chan);
    GETPARTITION(part, partition, -1);
    Path path(partition->GetFullPath());
    FRESULT fres;
    fres = fm->fs_write_sector(&path, channel->buffer, track, sector);
    drive->set_error_fres(fres);
    state = e_idle;
    return 0;
}

int IecCommandChannel :: do_buffer_position(int chan, int pos)
{
    if ((chan < 0) || (chan > 14)) {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return ERR_SYNTAX_ERROR_CMD;
    }
    if ((pos < 0) || (pos > 255)) {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return ERR_SYNTAX_ERROR_CMD;
    }
    IecChannel *channel;
    channel = iec_drive->get_data_channel(chan);
    channel->pointer = pos;
    channel->reset_prefetch();
    set_error(ERR_ALL_OK);
    return ERR_ALL_OK;
}

int IecCommandChannel::do_set_current_partition(int part)
{
    GETPARTITION(part, partition, -1);
    drive->vfs->SetCurrentPartition(part);        
    set_error(ERR_PARTITION_OK, part);
    return 0;
}

int IecCommandChannel::do_change_dir(filename_t& dest)
{
    GETPARTITION(dest.partition, prt, -1);
    DBGIECV("Partition %d ('%s') Change dir %s:%s\n", dest.partition, prt->GetFullPath(), dest.path.c_str(), dest.filename.c_str());

    // first get names in more usable format
    iec_path_to_fs_path(dest.path);
    char fatname[48];
    petscii_to_fat(dest.filename.c_str(), fatname, 48);

    if(!prt->cd(dest.path.c_str())) {
        drive->get_command_channel()->set_error(ERR_DIRECTORY_ERROR, prt->GetPartitionNumber());
        state = e_error;
        return -1;
    }
    if(!prt->cd(fatname)) {
        drive->get_command_channel()->set_error(ERR_DIRECTORY_ERROR, prt->GetPartitionNumber());
        state = e_error;
        return -1;
    }
    DBGIECV("Result of CD: '%s'\n", prt->GetFullPath());
    return 0;
}

int IecCommandChannel::do_make_dir(filename_t& dest)
{
    mstring work;
    const char *fullpath = ConstructPath(work, dest, e_folder, e_read);
    if (fullpath) {
        DBGIECV("Directory to create: %s\n", fullpath);
        FRESULT fres = fm->create_dir(fullpath);
        drive->set_error_fres(fres);
    } else {
        return ERR_DIRECTORY_ERROR;
    }
    return 0;
}

int IecCommandChannel::do_remove_dir(filename_t& dest)
{
    mstring work;
    const char *fullpath = ConstructPath(work, dest, e_folder, e_read);
    if (fullpath) {
        DBGIECV("Directory to remove: %s\n", fullpath);
        FRESULT fres = fm->delete_file(fullpath);
        if (fres == FR_DENIED) {
            return ERR_FILE_EXISTS;
        } else {
            drive->set_error_fres(fres);
        }
    } else {
        return ERR_DIRECTORY_ERROR;
    }
    return 0;
}

int IecCommandChannel::do_copy(filename_t& dest, filename_t sources[], int n)
{
    // Curiously, an original drive takes the file type from the FIRST file it copies.
    // So in order to know the destination extension, we'd need to first open the first file
    // using wildcards and then see what file was opened.
    mstring work;
    FileInfo info(48);
    const char *source1 = ConstructPath(work, sources[0], e_any, e_read);
    if (!source1) {
        drive->get_command_channel()->set_error(ERR_PARTITION_ERROR, sources[0].partition);
        return 0;
    }
    FRESULT fres = fm->fstat(source1, info);
    if (fres != FR_OK) {
        return ERR_FILE_NOT_FOUND;
    }
    // convert FAT name to CBM name
    char cbm_name[24];
    filetype_t ftype = e_any;
    IecPartition::CreateIecName(&info, cbm_name, ftype);
    // ftype is now set to the type of the first original file.

    // Now the real copy action can begin
    File *fi, *fo;
    const char *destpath = ConstructPath(work, dest, ftype, e_write);    
    if (!destpath) {
        drive->get_command_channel()->set_error(ERR_PARTITION_ERROR, dest.partition);
        return 0;
    }
    DBGIECV("Destination: %s\n", destpath);
    fres = fm->fopen(destpath, FA_CREATE_NEW | FA_WRITE, &fo);
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
        return 0;
    }
    // Output file is now open, let's copy data into it
    uint8_t *databuf = new uint8_t[32768];

    for(int i=0;i<n;i++) {
        const char *frompath = ConstructPath(work, sources[i], e_any, e_read);
        if (frompath) {
            DBGIECV("  %d. %s\n", i, frompath);
        } else {
            drive->get_command_channel()->set_error(ERR_PARTITION_ERROR, sources[i].partition);
            break;
        }
        fres = fm->fopen(frompath, FA_READ, &fi);
        if (fres != FR_OK) {
            drive->set_error_fres(fres);
            break;
        }
        uint32_t bytes_read, bytes_written;
        do {
            fres = fi->read(databuf, 32768, &bytes_read);
            if (fres == FR_OK) {
                fres = fo->write(databuf, bytes_read, &bytes_written);
            }
            if (fres != FR_OK) {
                drive->set_error_fres(fres);
                break;
            }
        } while (bytes_read > 0);

        fm->fclose(fi);
        if (fres != FR_OK)
            break;
    }
    delete databuf;
    fm->fclose(fo);
    return 0;
}

int IecCommandChannel::do_initialize()
{
    return ERR_DOS;
}

int IecCommandChannel::do_format(uint8_t *name, uint8_t id1, uint8_t id2)
{
    printf("Format: %s %02x %02x\n", name, id1, id2);
    return 0;
}

int IecCommandChannel::do_rename(filename_t &src, filename_t &dest)
{
    mstring works, workd;
    const char *src_path = ConstructPath(works, src, e_any, e_read);
    if (!src_path) {
        return ERR_PARTITION_ERROR;
    }
    FileInfo info(48);
    FRESULT fres = fm->fstat(src_path, info);
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
        return 0;
    }
    // convert FAT name to CBM name
    char cbm_name[24];
    filetype_t ftype = e_any;
    IecPartition::CreateIecName(&info, cbm_name, ftype);
    // ftype is now set to the type of the first original file.
    
    const char *dest_path = ConstructPath(workd, src, ftype, e_write);
    if (!dest_path) {
        return ERR_PARTITION_ERROR;
    }

    fres = fm->rename(src_path, dest_path);
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
    }
    return 0;
}

int IecCommandChannel::do_scratch(filename_t filenames[], int n)
{
    DBGIECV("Scratch %d files:\n", n);
    mstring work;
    for(int i=0;i<n;i++) {
        const char *fp = ConstructPath(work, filenames[i], e_any, e_not_set);
        if (fp) {
            DBGIECV("  %d. %s\n", i, fp);
        } else {
            drive->get_command_channel()->set_error(ERR_PARTITION_ERROR, filenames[i].partition);
            break;
        }
        FRESULT fres = fm->delete_file(fp);
        if (fres != FR_OK) {
            drive->set_error_fres(fres);
        }
    }
    return 0;
}

int IecCommandChannel::do_cmd_response(uint8_t *data, int len)
{
    memcpy(buffer, data, len);
    last_byte = len - 1;
    pointer = 0;
    prefetch = 0;
    prefetch_max = len;
    state = e_status;
    drive->set_error(0, 0, 0);
    return 0;
}

int IecCommandChannel::do_set_position(int chan, uint32_t pos)
{
    printf("Set File position to %lu on chan %d\n", pos, chan);
    return 0;
}


int IecCommandChannel::ext_open_file(const char *filenameOrCommand)
{
    strncpy((char *) wr_buffer, filenameOrCommand, 63);
    wr_buffer[63] = 0;
    wr_pointer = strlen((char *) wr_buffer);
    push_command(0x60);
    return push_command(0x00);
}

int IecCommandChannel::ext_close_file(void)
{
    // Do nothing
    return IEC_OK;
}

t_channel_retval IecCommandChannel::push_command(uint8_t b)
{
    int err;
    switch (b) {
    case 0x60:
        reset_prefetch();
        break;
    case 0xE0:
    case 0xF0:
        pointer = 0;
        wr_pointer = 0;
        break;
    case 0x00: // end of data, command received in buffer
        wr_buffer[wr_pointer] = 0;
        if (wr_pointer) {
            drive->set_error(0, 0, 0);
            err = parser->execute_command(wr_buffer, wr_pointer);
            if (err) set_error(err);
        }
        wr_pointer = 0;
        break;
    default:
        printf("Error on channel %d. Unknown command: %b\n", channel, b);
    }
    return IEC_OK;
}

bool IecPartition::IsValid()
{
    return fm->is_path_valid(GetFullPath());
}
