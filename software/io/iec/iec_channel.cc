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
    if ((state == e_error) || (state == e_complete)) {
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
    if ((state == e_error) || (state == e_complete)) {
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
    if (curblk->valid_bytes == 0) {
        state = e_complete; // end should have triggered already
        last_byte = -1;
        pointer = 0;
        prefetch = 0;
        prefetch_max = 0;
        return 0;
    }

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
        if ((name_to_open.access == e_write) || (name_to_open.access == e_append)) {
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
                //res = f->sync();
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

#define GETPARTITION(part, partition, reterr) \
    IecPartition *partition = drive->vfs->GetPartition(part); \
    if (!partition) { \
        drive->get_command_channel()->set_error(ERR_PARTITION_ERROR, drive->vfs->GetTargetPartitionNumber(part)); \
        return reterr; \
    }

static void iec_path_to_fs_path(mstring &path)
{
    // Path conversion needs to take place, because
    // in this context, _ means "..", and when the path starts
    // with a /, it should be stripped off, while // means root,
    // which corresponds to starting with / in Ultimate VFS.
    if (path[0] == '/' && path[1] != '/') {
        mstring stripped(path.c_str() + 1);
        path = stripped;
    }
    path.replace("_", "..");
    path.replace("//", "/");
}

static bool iec_file_type_matches(filetype_t requested, filetype_t found)
{
    return requested == e_any || found == e_any || requested == found;
}

static bool iec_name_has_wildcards(const char *name)
{
    return name && (strchr(name, '*') || strchr(name, '?'));
}

static void append_path_component(mstring& path, const char *component)
{
    if (!component || !component[0]) {
        return;
    }
    if (path.length()) {
        const char *p = path.c_str();
        if (p[path.length() - 1] != '/') {
            path += "/";
        }
    }
    path += component;
}

static void partition_relative_to_full_path(IecPartition *partition, Path& relative, mstring& full_path)
{
    full_path = partition->GetRootPath();
    full_path += relative.get_path() + 1;
}

static FRESULT find_rendered_iec_child(FileManager *fm, const char *full_dir,
                                       const char *iec_pattern, filetype_t ftype,
                                       bool allow_dirs, bool allow_files,
                                       bool allow_wildcards,
                                       mstring& actual_name, FileInfo *out_info)
{
    if (!allow_wildcards && iec_name_has_wildcards(iec_pattern)) {
        return FR_NO_FILE;
    }

    Directory *dir = NULL;
    FRESULT fres = fm->open_directory(full_dir, &dir);
    if (fres != FR_OK) {
        return fres;
    }

    FileInfo info(INFO_SIZE);
    while (dir->get_entry(info) == FR_OK) {
        bool is_dir = (info.attrib & AM_DIR) != 0;
        if ((info.attrib & (AM_VOL | AM_HID)) || !info.lfname[0]) {
            continue;
        }
        if ((is_dir && !allow_dirs) || (!is_dir && !allow_files)) {
            continue;
        }

        char iec_name[24];
        filetype_t found_type = e_any;
        IecPartition::CreateIecName(&info, iec_name, found_type);
        if (!is_dir && !iec_file_type_matches(ftype, found_type)) {
            continue;
        }
        if (is_dir && ftype != e_any && ftype != e_folder) {
            continue;
        }
        if (!pattern_match(iec_pattern, iec_name, false)) {
            continue;
        }

        actual_name = info.lfname;
        if (out_info) {
            out_info->copyfrom(&info);
        }
        delete dir;
        return FR_OK;
    }

    delete dir;
    return FR_NO_FILE;
}

static FRESULT resolve_directory_path(FileManager *fm, IecPartition *partition,
                                      mstring path, mstring& full_path,
                                      mstring *relative_path = NULL)
{
    // printf("resolve_directory_path: %s (pwd = %s)\n", path.c_str(), partition->GetRelativePath());
    iec_path_to_fs_path(path);

    Path resolved;
    if (path[0] != '/') {
        resolved.cd(partition->GetRelativePath());
    }

    // Path requested(path.c_str()); // this doesnt work if the path starts with .. for example
    // printf("resolve_directory_path: path = %s. Requested = %s. Resolved before loop = %s.\n", path.c_str(), requested.get_path(), resolved.get_path());
    const char *components[16];
    int count = path.split('/', components, 16);

    for (int i = 0; i < count; i++) {
        const char *component = components[i]; //requested.getElement(i);
        char fat_component[52];
        const char *direct_component = component;

        if (!strcmp(component, ".") || !strcmp(component, "..")) {
            if (!resolved.cd(component)) { // not possible to move
                return FR_NO_PATH;
            }
            continue;
        } 

        petscii_to_fat(component, fat_component, 52);
        // printf("Fat component: %s\n", fat_component);
        direct_component = fat_component;

        Path direct_path(&resolved);
        if (direct_path.cd(direct_component)) {
            mstring direct_full_path;
            partition_relative_to_full_path(partition, direct_path, direct_full_path);

            // printf("Full Path is now: %s\n", direct_full_path.c_str());
            Directory *test_dir = NULL;
            FRESULT direct_fres = fm->open_directory(direct_full_path.c_str(), &test_dir);
            if (direct_fres == FR_OK) {
                delete test_dir;
                resolved.cd(direct_component);
                // printf("Resolved: %s\n", resolved.get_path());
                continue;
            }
        }

        mstring current_full_path;
        partition_relative_to_full_path(partition, resolved, current_full_path);
        // printf("Current Full Path: %s\n", current_full_path.c_str());
        mstring actual_name;
        FRESULT fres = find_rendered_iec_child(fm, current_full_path.c_str(), component,
                                               e_folder, true, false, false,
                                               actual_name, NULL);
        // printf("Fres = %s. Actual name = %s\n", FileSystem::get_error_string(fres), actual_name.c_str());
        if (fres != FR_OK) {
            return fres;
        }
        resolved.cd(actual_name.c_str());
    }

    partition_relative_to_full_path(partition, resolved, full_path);
    // printf("Full Path: %s\n", full_path.c_str());
    if (relative_path) {
        *relative_path = resolved.get_path();
    }
    return FR_OK;
}

static FRESULT resolve_existing_iec_path(FileManager *fm, IecPartition *partition,
                                         filename_t& name, filetype_t ftype,
                                         bool allow_dirs, bool allow_files,
                                         bool allow_wildcards,
                                         mstring& resolved, FileInfo *out_info = NULL)
{
    mstring full_dir;
    FRESULT fres = resolve_directory_path(fm, partition, name.path, full_dir);
    if (fres != FR_OK) {
        return fres;
    }

    mstring actual_name;
    fres = find_rendered_iec_child(fm, full_dir.c_str(), name.filename.c_str(),
                                   ftype, allow_dirs, allow_files, allow_wildcards,
                                   actual_name, out_info);
    if (fres != FR_OK) {
        return fres;
    }

    resolved = full_dir;
    append_path_component(resolved, actual_name.c_str());
    return FR_OK;
}

static FRESULT resolve_directory_target(FileManager *fm, IecPartition *partition,
                                        filename_t& name, mstring& full_path,
                                        mstring& relative_path)
{
    FRESULT fres = resolve_directory_path(fm, partition, name.path, full_path, &relative_path);
    if (fres != FR_OK || name.filename.length() == 0) {
        return fres;
    }

    char fatname[52];
    petscii_to_fat(name.filename.c_str(), fatname, 52);

    Path direct_relative(relative_path.c_str());
    if (direct_relative.cd(fatname)) {
        mstring direct_full_path;
        partition_relative_to_full_path(partition, direct_relative, direct_full_path);

        Directory *test_dir = NULL;
        FRESULT direct_fres = fm->open_directory(direct_full_path.c_str(), &test_dir);
        if (direct_fres == FR_OK) {
            delete test_dir;
            full_path = direct_full_path;
            relative_path = direct_relative.get_path();
            return FR_OK;
        }
    }

    mstring actual_name;
    fres = find_rendered_iec_child(fm, full_path.c_str(), name.filename.c_str(),
                                   e_folder, true, false, false,
                                   actual_name, NULL);
    if (fres != FR_OK) {
        return fres;
    }

    Path resolved_relative(relative_path.c_str());
    resolved_relative.cd(actual_name.c_str());
    partition_relative_to_full_path(partition, resolved_relative, full_path);
    relative_path = resolved_relative.get_path();
    return FR_OK;
}

static FRESULT open_by_rendered_iec_name(FileManager *fm, IecPartition *partition,
                                         filename_t& name, filetype_t ftype,
                                         uint8_t flags, File **file, mstring& resolved)
{
    FRESULT fres = resolve_existing_iec_path(fm, partition, name, ftype,
                                             false, true, true, resolved);
    if (fres != FR_OK) {
        return fres;
    }
    return fm->fopen(resolved.c_str(), flags, file);
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
        state = e_dir; // This causes a -1 to be returned next time this function is called
        return 0;
    }
        
    // Skip volume entries
    if (info.attrib & AM_VOL) {
        return 1;
    }    

    // convert FAT name to CBM name
    char cbm_name[24];
    filetype_t ftype = e_any;
    IecPartition::CreateIecName(&info, cbm_name, ftype);
    if (ftype == e_any) {
        ftype = e_seq;
    }

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

int IecChannel :: setup_partition_read()
{
    DBGIEC("Setup partition read\n");
    drive->get_command_channel()->set_error(ERR_ALL_OK, 0, 0);
    state = e_partlist;
    part_idx = 1;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 32;
    last_byte = -1;
    dir_free = 0;
    memcpy(buffer, c_header, 32);
    memcpy(buffer+8, "ULTIMATE HD", 11);
    memcpy(buffer+26, "UL 64", 5);
    return 0;
}

int IecChannel :: setup_directory_read()
{
    DBGIEC("Setup dir read\n");
    char fatname[48];

    // previously not closed??
    if (dir) {
        delete dir;
    }    

    GETPARTITION(name_to_open.file.partition, partition, -1);
    petscii_to_fat(name_to_open.file.filename.c_str(), fatname, 48);

    mstring work;
    FRESULT fres = resolve_directory_path(fm, partition, name_to_open.file.path, work);
    if (fres != FR_OK) {
        drive->set_error(ERR_DIRECTORY_ERROR, drive->vfs->GetTargetPartitionNumber(name_to_open.file.partition), 0);
        state = e_error;
        return -1;
    }

    DBGIECV("Full Path = %s, filename = %s\n", work.c_str(), fatname);

    FileInfo info(40);
    fres = fm->open_directory(work.c_str(), &dir, &info);
    if (fres != FR_OK) {
        printf("opening dir failed %s\n", FileSystem::get_error_string(fres));
        drive->get_command_channel()->set_error(ERR_DRIVE_NOT_READY, name_to_open.file.partition);
        state = e_error;
        return -1;
    }

    Path path(work.c_str());
    uint32_t cluster_size;
    fres = fm->get_free(&path, dir_free, cluster_size);
    drive->get_command_channel()->set_error(ERR_ALL_OK, 0, 0);
    state = e_dir;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 32;
    last_byte = -1;
    memcpy(buffer, c_header, 32);
    buffer[4] = (uint8_t) drive->vfs->GetTargetPartitionNumber(name_to_open.file.partition);

    IecPartition *part = drive->vfs->GetPartition(name_to_open.file.partition);

    if (info.fs->supports_direct_sector_access()) { // might be CBM image!
        dir->get_entry(info); // first one SHOULD be the volume label
        if ((info.attrib & AM_VOL) && (info.name_format & NAME_FORMAT_CBM)) {
            printf("Volume label found: %s\n", info.lfname);
            for (int i = 0; i < 23; i++) {
                buffer[8 + i] = info.lfname[i] & 0x7F;
            }
            buffer[8 + 16] = 0x22;
            return 0;
        }
    }

    //const char *fullname = part->GetRelativePath();
    const char *pp = partition->GetName(); // new feature!
    int pos = 8;
    int len = strlen(pp);
    if (len > 16) {
        pp += (len - 16);
    }
    while ((pos < 24) && (*pp))
        buffer[pos++] = toupper(*(pp++));
    return 0;
}

void print_file(filename_t& file)
{
    printf("P=%d, Path='%s', Filename='%s', Wildcard: %s\n",
        file.partition, file.path.c_str(), file.filename.c_str(), file.has_wildcard?"true":"false" );
}

int IecChannel :: setup_file_access()
{
    drive->set_error(0, 0, 0);
    state = e_error;

    if (name_to_open.access == e_not_set) {
        name_to_open.access = (channel == 1) ? e_write : e_read;
    }

    if (name_to_open.filetype == e_any) {
        if (channel < 2) {
            name_to_open.filetype = e_prg;
        } else if (name_to_open.access != e_read) { // for writes on other channels, default to seq.
            name_to_open.filetype = e_seq;
        }
    }

    DBGIECV("Name to open: Type %d. Access: %b\n", name_to_open.filetype, name_to_open.access);
#if IECDEBUG > 0
    print_file(name_to_open.file);
#endif
    mstring work;
    const char *full_path = ConstructPath(work, name_to_open.file, name_to_open.filetype, name_to_open.access );
    if (!full_path) {
        drive->set_error(ERR_PARTITION_ERROR, drive->vfs->GetTargetPartitionNumber(name_to_open.file.partition), 0);
        return 0;
    }

    uint8_t flags;
    switch(name_to_open.access) {
    case e_append:
        flags = FA_WRITE;
        break;
    case e_write:
        if (name_to_open.replace) {
            flags = (FA_WRITE | FA_CREATE_ALWAYS);
        } else {
            flags = (FA_WRITE | FA_CREATE_NEW);
        }
        break;
    default:
        flags = FA_READ; 
    }
    if (name_to_open.filetype == e_rel) {
        flags |= ( FA_READ | FA_WRITE );
        if (name_to_open.record_size != 0) {
            flags |= FA_OPEN_ALWAYS;
        }
    }

    DBGIECV("Setup File Access %s %02x\n", full_path, flags);

    FRESULT fres = fm->fopen(full_path, flags, &f);
    if ((fres == FR_NO_FILE) && (name_to_open.access == e_read)) {
        GETPARTITION(name_to_open.file.partition, partition, 0);
        fres = open_by_rendered_iec_name(fm, partition, name_to_open.file,
                                         name_to_open.filetype, flags, &f, work);
        if (fres == FR_OK) {
            full_path = work.c_str();
            DBGIECV("Directory-assisted open resolved to %s\n", full_path);
        }
    }
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
        return 0;
    }

    last_byte = -1;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 512;
    state = e_file;

    if (name_to_open.filetype == e_rel) {
        uint32_t tr;
        if (!f->get_size()) { // the file must be newly created, because its size is 0.
            uint16_t wrd = name_to_open.record_size;
            fres = f->write(&wrd, 2, &tr);
            drive->set_error_fres(fres);
            if (fres == FR_OK) {
                recordSize = name_to_open.record_size;
                state = e_record;
            }
        } else { // file already exists
            uint16_t wrd;
            fres = f->read(&wrd, 2, &tr);
            if (wrd >= 256) {
                DBGIECV("WARNING: Illegal record size in .rel file...Is it a REL file at all? (%d)\n", wrd);
                state = e_error;
                drive->set_error(ERR_RECORD_NOT_PRESENT, 0, 0);
                return ERR_RECORD_NOT_PRESENT;
            }
            recordSize = (uint8_t) wrd;
            DBGIECV("Opened existing relative file. Record size found is: %d. Expected: %d\n", recordSize, name_to_open.record_size);
            if ((name_to_open.record_size != 0) && (recordSize != name_to_open.record_size)) {
                state = e_error;
                drive->set_error(ERR_RECORD_NOT_PRESENT, 0, 0);
                return ERR_RECORD_NOT_PRESENT;
            }
            drive->set_error_fres(fres);
            state = e_record;
            int err = seek_record(1, 1);
            if (err != ERR_ALL_OK) {
                drive->set_error(err, 0, 0);
                return err;
            }
        }
    } else if (name_to_open.access == e_append) {
        FRESULT fres = f->seek(f->get_size());
        if (fres != FR_OK) {
            drive->get_command_channel()->set_error(ERR_FRESULT_CODE, fres);
            state = e_error;
            return 0;
        }
    }

    if (state == e_file && name_to_open.access == e_read) {
        if (f) {
            curblk->valid_bytes = 0;
            // queue up one next block
            f->read(nxtblk->bufdata, 512, &nxtblk->valid_bytes);
            DBGIECV("Initial read %d bytes.\n", nxtblk->valid_bytes);
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
    DBGIECV("Open file. Raw Filename = '%s'\n", buffer);
    int parse_err = parse_open((const char *)buffer, name_to_open);
    if (parse_err) {
        state = e_error;
        drive->set_error(parse_err, 0, 0);
        return -1;
    }

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
        DBGIEC("Unknown stream mode\n");
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

int IecChannel::seek_record(int recordNumber, int offset)
{
    const uint32_t c_header = 2; // 2 bytes for record size in the beginning of the file
    // flush
    write_record();

    if (recordNumber != 0)
        recordNumber--;  // Record numbers are starting from 1.
    if (offset != 0)
        offset--; // Offset starts from 1, too.. we are 0 based

    DBGIECV("SEEK Record Number #%d. Offset = %d\n", recordNumber, offset);
    if (!f) {
        return ERR_FILE_NOT_OPEN;
    }
    if (!recordSize) { // not a (valid) rel file
        return ERR_FILE_TYPE_MISMATCH;
    }

    uint32_t targetPosition = c_header + (recordNumber * recordSize); // + offset; // reserve 2 bytes for control (record Size)
    recordOffset = targetPosition;
    uint32_t minimumFileSize = targetPosition + recordSize; // Fileshould be at least one record larger than the begin position of the one requested
    uint32_t currentSize = f->get_size();
    FRESULT fres;
    int err = ERR_ALL_OK;
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

        DBGIECV("Append REL file until size %d (was %d)\n", minimumFileSize, currentSize);
        uint32_t remain = minimumFileSize - currentSize;
        block[0] = 0xFF;
        while (remain >= recordSize) {
            fres = f->write(block, recordSize, &tr);
            remain -= recordSize;
        }
        delete[] block;
        err = ERR_RECORD_NOT_PRESENT;
    }

    fres = f->seek(targetPosition);
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
        return 0;
    }

    if (offset > recordSize - 1) {
        offset = recordSize - 1;
        err = ERR_OVERFLOW_IN_RECORD;
    }

    t_channel_retval ret = read_record(offset);
    if (ret != IEC_OK) {
        err = ERR_READ_ERROR;
    }
    return err;
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
    delete parser;
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

const char *IecChannel :: ConstructPath(mstring& work, filename_t& name, filetype_t ftype, fileaccess_t acc)
{
    const char *types[] = { ".???", ".prg", ".seq", ".usr", ".rel", "" };

    GETPARTITION(name.partition, partition, NULL);
    char fatname[52];
    petscii_to_fat(name.filename.c_str(), fatname, 52);
    // printf("After petscii_to_fat: '%s'\n", fatname);
    const char *ext = types[(int)ftype];
    if ((acc == e_read) || (ftype != e_any)) { // for reads, .??? is allowed, but for writes it is not
        add_extension(fatname, ext, 48);
    }

    FRESULT dir_fres = resolve_directory_path(fm, partition, name.path, work);
    if (dir_fres != FR_OK) {
        return NULL;
    }
    append_path_component(work, fatname);
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
    IecChannel *channel = drive->get_data_channel(chan);
    GETPARTITION(part, partition, -1);
    Path path(partition->GetFullPath());
    FRESULT fres;
    fres = fm->fs_read_sector(&path, channel->buffer, track, sector);
    if (fres == FR_DENIED) {
        drive->set_error(ERR_DENIED, track, sector);
    } else {
        drive->set_error_fres(fres);
    }
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
    IecChannel *channel = drive->get_data_channel(chan);
    GETPARTITION(part, partition, -1);
    Path path(partition->GetFullPath());
    FRESULT fres;
    fres = fm->fs_write_sector(&path, channel->buffer, track, sector);
    if (fres == FR_DENIED) {
        drive->set_error(ERR_DENIED, track, sector);
    } else {
        drive->set_error_fres(fres);
    }
    state = e_idle;
    return 0;
}

int IecCommandChannel::do_block_allocate(int chan, int part, int track, int sector, bool alloc)
{
    if ((chan < 0) || (chan > 14)) {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return ERR_SYNTAX_ERROR_CMD;
    }
    if (part >= MAX_PARTITIONS) {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return ERR_SYNTAX_ERROR_CMD;
    }
    IecChannel *channel = drive->get_data_channel(chan);
    GETPARTITION(part, partition, -1);
    Path path(partition->GetFullPath());
    FRESULT fres;
    fres = fm->fs_allocate_sector(&path, track, sector, alloc);
    if (fres == FR_DENIED) {
        drive->set_error(ERR_DENIED, track, sector);
    } else {
        drive->set_error_fres(fres);
    }
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
    channel = drive->get_data_channel(chan);
    channel->pointer = pos;
    channel->reset_prefetch();
    set_error(ERR_ALL_OK);
    return ERR_ALL_OK;
}

int IecCommandChannel::do_set_current_partition(int part)
{
    GETPARTITION(part, partition, -1);
    drive->vfs->SetCurrentPartition(part);        
    set_error(ERR_PARTITION_OK, drive->vfs->GetTargetPartitionNumber(0));
    return 0;
}

int IecCommandChannel::do_change_dir(filename_t& dest)
{
    GETPARTITION(dest.partition, prt, -1);
    DBGIECV("Partition %d ('%s') Change dir %s:%s\n", dest.partition, prt->GetFullPath(), dest.path.c_str(), dest.filename.c_str());

    mstring full_path;
    mstring relative_path;
    FRESULT fres = resolve_directory_target(fm, prt, dest, full_path, relative_path);
    if ((fres != FR_OK) || !prt->cd(relative_path.c_str())) {
        drive->get_command_channel()->set_error(ERR_DIRECTORY_ERROR, prt->GetPartitionNumber());
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
        if ((fres == FR_NO_FILE || fres == FR_NO_PATH) && !dest.has_wildcard) {
            GETPARTITION(dest.partition, partition, 0);
            fres = resolve_existing_iec_path(fm, partition, dest, e_folder,
                                             true, false, false, work);
            if (fres == FR_OK) {
                DBGIECV("Directory-assisted RD resolved to %s\n", work.c_str());
                fres = fm->delete_file(work.c_str());
            }
        }
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
    FRESULT fres = source1 ? fm->fstat(source1, info) : FR_NO_PATH;
    if (fres != FR_OK) {
        GETPARTITION(sources[0].partition, partition, 0);
        fres = resolve_existing_iec_path(fm, partition, sources[0], e_any,
                                         false, true, sources[0].has_wildcard,
                                         work, &info);
        source1 = work.c_str();
    }
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
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
            DBGIECV("  %d. unresolved by canonical path\n", i);
        }
        fres = frompath ? fm->fopen(frompath, FA_READ, &fi) : FR_NO_PATH;
        if (fres != FR_OK) {
            GETPARTITION(sources[i].partition, partition, 0);
            fres = open_by_rendered_iec_name(fm, partition, sources[i], e_any,
                                             FA_READ, &fi, work);
            frompath = work.c_str();
            if (fres == FR_OK) {
                DBGIECV("  %d. directory-assisted source %s\n", i, frompath);
            }
        }
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
    delete[] databuf;
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
    FileInfo info(48);
    FRESULT fres = src_path ? fm->fstat(src_path, info) : FR_NO_PATH;
    if (fres != FR_OK) {
        GETPARTITION(src.partition, partition, 0);
        fres = resolve_existing_iec_path(fm, partition, src, e_any,
                                         false, true, src.has_wildcard,
                                         works, &info);
        src_path = works.c_str();
    }
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
        return 0;
    }
    // convert FAT name to CBM name
    char cbm_name[24];
    filetype_t ftype = e_any;
    IecPartition::CreateIecName(&info, cbm_name, ftype);
    // ftype is now set to the type of the original file.
    
    const char *dest_path = ConstructPath(workd, dest, ftype, e_write);
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
    int scratched = 0;
    for(int i=0;i<n;i++) {
        int scratched_this_file = 0;
        const char *fp = ConstructPath(work, filenames[i], e_any, e_read); // If read is not set, the extension will not be set to .???
        if (fp) {
            DBGIECV("  %d. %s\n", i, fp);
        } else {
            DBGIECV("  %d. unresolved by canonical path\n", i);
        }
        FRESULT fres = FR_NO_PATH;
        if (fp) {
            do {
                fres = fm->delete_file(fp);
                if (fres == FR_OK) {
                    scratched ++;
                    scratched_this_file ++;
                }
            } while(fres == FR_OK);
        }
        if (!scratched_this_file && !filenames[i].has_wildcard &&
                (fres == FR_NO_FILE || fres == FR_NO_PATH)) {
            GETPARTITION(filenames[i].partition, partition, 0);
            fres = resolve_existing_iec_path(fm, partition, filenames[i], e_any,
                                             false, true, false, work);
            if (fres == FR_OK) {
                DBGIECV("  %d. directory-assisted scratch %s\n", i, work.c_str());
                fres = fm->delete_file(work.c_str());
                if (fres == FR_OK) {
                    scratched ++;
                }
            }
        }
    }
    if (scratched == 0) {
        return ERR_FILE_NOT_FOUND;
    }
    set_error(ERR_FILES_SCRATCHED, scratched);
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

int IecCommandChannel::do_pwd_command()
{
    IecPartition *part = drive->vfs->GetPartition(drive->vfs->GetTargetPartitionNumber(0)); // get current partition
    if (!part) {
        return ERR_PARTITION_ERROR;
    }
    buffer[255] = 0; // ensure string terminator
    sprintf((char *) buffer, "%d:", part->GetPartitionNumber());
    int pl = strlen((char *) buffer);    
    const char *src = part->GetRelativePath();
    for(int i=0;i < 255-pl;i++) {
        buffer[pl+i] = toupper(src[i]);
        if (src[i] == 0) {
            break; 
        }
    }
    int len = strlen((char *) buffer);
    last_byte = len - 1;
    pointer = 0;
    prefetch = 0;
    prefetch_max = len;
    state = e_status;
    drive->set_error(0, 0, 0);
    return 0;
}

int IecCommandChannel::do_set_position(int chan, uint32_t pos, int recnr, int recoffset)
{
    DBGIECV("Set File position to %u on chan %d. RecNr %d:%d\n", pos, chan, recnr, recoffset);
    if ((chan < 0) || (chan > 14)) {
        return ERR_SYNTAX_ERROR_CMD;
    }
    IecChannel *channel = drive->get_data_channel(chan);
    if (!channel->f) {
        return ERR_FILE_NOT_OPEN;
    }
    FRESULT fres;
    switch(channel->state) {
    case e_buffer:
        return do_buffer_position(chan, pos);
    case e_file:
        fres = channel->f->seek(pos);
        if (fres != FR_OK) {
            drive->set_error_fres(fres);
            return 0;
        }
        channel->curblk->valid_bytes = 0;
        channel->nxtblk->valid_bytes = 0;
        fres = channel->f->read(channel->nxtblk->bufdata, 512, &channel->nxtblk->valid_bytes);
        if (fres != FR_OK) {
            drive->set_error_fres(fres);
            channel->state = e_error;
            return 0;
        }
        if (channel->read_block()) {
            drive->set_error(ERR_READ_ERROR, 0, 0);
            channel->state = e_error;
            return 0;
        }
        channel->pointer = 0;
        channel->reset_prefetch();
        channel->recordOffset = pos;
        drive->set_error(ERR_ALL_OK, 0, 0);
        state = e_idle;
        return 0;
    case e_record:
        {
            int err = channel->seek_record(recnr, recoffset);
            state = e_idle;
            return err;
        }
    default:
        return ERR_FILE_NOT_OPEN;
    }  
}

int IecCommandChannel::do_get_partition_info(int part)
{
    IecPartition *p = drive->vfs->GetPartition(part);
    buffer[30] = 0x0d;
    memset(buffer, 0, 30);

    if (p) {
        drive->set_error(0, 0, 0);
        buffer[1] = 1;
        buffer[2] = (uint8_t)part;

        char cbm_name[24];
        FileInfo info(40);
        filetype_t ftype = e_any;
        strncpy(info.lfname, p->GetRootPath(), info.lfsize);
        IecPartition::CreateIecName(&info, cbm_name, ftype);

        strncpy((char *)(buffer+3), cbm_name, 16);
        buffer[27] = 0xFF; // for now always reporting 0xFF0000 as partition size
    } else {
        drive->set_error(77, part, 0);
    }

    last_byte = 30;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 30;
    state = e_status;
    return 0;

// Byte 0
// - Partition type
// 0 not created
// 1 Native Mode
// 2 1541 Emulation Mode
// 3 1571 Emulation Mode
// 4 1581 Emulation Mode
// 5 1581 CP/M Emulation Mode
// 6 Print Buffer
// 7 Foreign Mode
// 255 System
// 
// Byte1        - CHR$(0) (reserved)
// Byte2        - Partition number
// Bytes 3-18   - Partition name as displayed in the partition directory
// Byte 19      - Starting system address of partition (high byte} 
// Byte 20      - Starting system address of partition (middle byte)
// Byte 21      - Starting system address of partition (low byte)
// Bytes 22-26  - CHR$(0) (reserved)
// Byte 27      - Size of partition (high byte)
// Byte 28      - Size of partition (middle byte)
// Byte 29      - Size of partition (low byte)
// Byte 30      - CHR$(13)
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

void IecFileSystem :: LoadPartitions(const char *path, const char *file)
{
    File *fi;
    FRESULT fres = fm->fopen(path, file, FA_READ, &fi);
    if (fres == FR_OK) {
        LoadPartitions(fi);
        fm->fclose(fi);
    }
}

FRESULT IecFileSystem :: SavePartitions(const char *path, const char *filename)
{
    File *fo;
    uint32_t tr;

    JSON_Object *root = JSON::Obj();
    JSON_List *json_parts = JSON::List();
    root->add("version", 1);
    root->add("partitions", json_parts);

    for(int i=1; i<MAX_PARTITIONS; i++) {
        if (partitions[i]) {
            json_parts->add(JSON::Obj()
                ->add("number", i)
                ->add("path", partitions[i]->GetRootPath())
                ->add("name", partitions[i]->GetName())
            );
        }
    }

    FRESULT fres = fm->fopen(path, filename, FA_CREATE_ALWAYS | FA_WRITE, &fo);
    if (fres == FR_OK) {
        const char *text = root->render();
        int len = strlen(text);
        uint32_t transferred = 0;
        fres = fo->write(text, len, &transferred);
        if ((fres == FR_OK) && (transferred != (uint32_t)len)) {
            fres = FR_DISK_ERR;
        }
        fm->fclose(fo);
    }
    delete root;
    return fres;
}

void IecFileSystem :: LoadPartitions(File *f)
{
    uint32_t size = f->get_size();
    if ((size > 12288) || (size < 8)) { // max 12K partition paths file
        return;
    }
    char *buffer = new char[size+1];
    uint32_t transferred;
    f->read(buffer, size, &transferred);
    buffer[transferred] = 0;

    JSON *root_json = NULL;
    int tokens = convert_text_to_json_objects(buffer, transferred, 2048, &root_json);
    if ((tokens > 0) && root_json && (root_json->type() == eObject)) {
        JSON *servers_json = ((JSON_Object *)root_json)->get("partitions");
        if (servers_json && (servers_json->type() == eList)) {
            JSON_List *servers = (JSON_List *)servers_json;
            for (int i=0; i < servers->get_num_elements(); i++) {
                JSON *entry_json = (*servers)[i];
                if (!entry_json || (entry_json->type() != eObject)) {
                    continue;
                }

                JSON_Object *entry = (JSON_Object *)entry_json;
                const char *name = entry->string_or("name", "PARTITION");
                const char *path = entry->string_or("path", "");
                const int part = entry->int_or("number", -1);
                if (!path[0] || !is_valid_partition_number(part)) {
                    continue;
                }
                if (partitions[part]) {
                    partitions[part]->SetRoot(path);
                    partitions[part]->SetName(name);
                } else {
                    add_partition(part, path, name);
                }
            }
        }
    }
    delete root_json;
    delete[] buffer;
}
