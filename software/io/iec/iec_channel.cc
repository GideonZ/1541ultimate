#include "iec_channel.h"
#include "dump_hex.h"
#include "rtc.h"
#include "iec_log.h"
#include "blockdev_file.h"
#include "filesystem_d64.h"
#include <stdlib.h>
#ifndef RUNS_ON_PC
#include "FreeRTOS.h"
#include "task.h"
#include "itu.h"
#endif

/* ------------------------------------------------------------------------------
 * The log (see iec_log.h): always one line for a command that leaves an error, an open
 * that fails and the first failure of a channel. With the drive's "Log Every Operation"
 * setting on, also one line for every other command, open and close, and for the last
 * line of each listing.
 * ------------------------------------------------------------------------------ */

// Whether the error channel now holds an error. 00 to 19 are not errors in CBM DOS, and 73
// is the answer to UI and the other resets, not a failure.
bool IecChannel::drive_failed(void)
{
    return (drive->last_error_code >= 20) && (drive->last_error_code != ERR_DOS);
}

#ifndef RUNS_ON_PC
// Other tasks print a character at a time, and a line of theirs printed into this one
// would split it where the syslog breaks lines. So the line goes to the internal log and
// the syslog with the scheduler suspended, starting a line of its own; both only write
// memory. The serial console gets it afterwards, as a wait on the UART must not hold
// every task.
static void emit_log_line(const char *line)
{
    vTaskSuspendAll();
    if (custom_outbyte) {
        if (outbyte_last != '\n') {
            custom_outbyte('\n');
        }
        for (const char *p = line; *p; p++) {
            if (*p == '\n') {
                custom_outbyte('\r');
            }
            custom_outbyte(*p);
        }
    }
    outbyte_last = '\n';
    xTaskResumeAll();
    for (const char *p = line; *p; p++) {
        if (*p == '\n') {
            console_outbyte('\r');
        }
        console_outbyte(*p);
    }
}
#endif

// Writes one line: what happened, the current partition and its working directory, the
// bytes involved, optionally a labelled second set of bytes, the error channel's answer and
// a sequence number.
// Bytes are passed with their length and never treated as a string, and every rendering is
// bounded by its buffer. The buffers are static to keep them off the IEC task's small
// stack; every caller holds the drive's lock (CR-6), so two lines cannot be built at once.
void IecChannel::log_line(const char *what, const uint8_t *payload, int len,
                          const char *label, const uint8_t *extra, int extra_len)
{
    static char txt[SOFTIEC_LOG_TEXT_SIZE];
    static char more[SOFTIEC_LOG_TEXT_SIZE];
    static char dir[SOFTIEC_LOG_TEXT_SIZE];
    static char err[80];

    // The drive's rendering of the error channel, without the carriage return it ends in,
    // which would split the line. Reading it here does not clear it.
    int n = drive->get_error_string(err);
    if ((n < 0) || (n >= (int)sizeof(err))) {
        n = 0;
    }
    while ((n > 0) && ((err[n - 1] == 0x0D) || (err[n - 1] == 0x0A))) {
        n--;
    }
    err[n] = 0;

    IecPartition *part = drive->vfs ? drive->vfs->GetPartition(0) : NULL;
    const char *cwd = part ? part->GetRelativePath() : NULL;
    softiec_log_text((const uint8_t *)cwd, cwd ? strlen(cwd) : 0, dir, sizeof(dir));
    softiec_log_text(payload, payload ? len : 0, txt, sizeof(txt));
    softiec_log_text(extra, extra ? extra_len : 0, more, sizeof(more));
    // The sequence number counts every line, so a reader can tell a line that was lost or
    // delivered twice on the way to the log from one the drive wrote twice.
    static unsigned sequence = 0;
    // Three renderings and at most about 250 characters around them.
    static char line[3 * SOFTIEC_LOG_TEXT_SIZE + 512];
    snprintf(line, sizeof(line), SOFTIEC_LOG_PREFIX "%s dev=%d chan=%d part=%d dir=\"%s\" len=%d txt=\"%s\"%s%s%s%s%s -> %s #%u\n",
             what, (int)drive->get_address(), channel, part ? part->GetPartitionNumber() : 0, dir,
             payload ? len : 0, txt, label ? " " : "", label ? label : "", label ? "=\"" : "",
             label ? more : "", label ? "\"" : "", err, ++sequence);
    int end = strlen(line);
    if ((end == 0) || (line[end - 1] != '\n')) { // cut short: it still ends the line
        if (end > (int)sizeof(line) - 2) {
            end = sizeof(line) - 2;
        }
        line[end] = '\n';
        line[end + 1] = 0;
    }
#ifdef RUNS_ON_PC
    fputs(line, stdout);
#else
    emit_log_line(line);
#endif
}

// One line the first time a channel fails after it was opened, so a channel that fails
// on every byte does not fill the log.
void IecChannel::log_fault(const char *what)
{
    if (fault_logged) {
        return;
    }
    fault_logged = true;
    static char detail[96];
    snprintf(detail, sizeof(detail), "channel fault in %s, state %d", what, (int)state);
    log_line(detail, NULL, 0, NULL, NULL, 0);
}

IecChannel::IecChannel(IecDrive *dr, int ch)
{
    fault_logged = false;
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
    recordMissing = false;
    recordOffset = 0;
    dataOffset = 0;
    recordDirty = false;
    dir_free = 0;
    buffer_partition = 0;
    pingpong = true;
    swap_buffers();
}

IecChannel::~IecChannel()
{
    close_file();
}

void IecChannel::reset(void)
{
    fault_logged = false;
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
        if (prefetch > last_byte) {
            // Already past it, for example a P into a record beyond its last byte.
            // Offering a negative count makes the UCI target copy that many bytes.
            fetched = 0;
            return IEC_NO_FILE;
        }
        if (prefetch + max_fetch > last_byte) { // example: prefetch = 0, last_byte = 1 => there are 2 bytes. if max_fetch = 2, it will be set to 2.
            max_fetch = last_byte - prefetch + 1;
            last = true;
        }
    } else {
        if (prefetch + max_fetch > prefetch_max) {
            max_fetch = (prefetch < prefetch_max) ? (prefetch_max - prefetch) : 0;
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
        // A talk pass that found the fifo full pops nothing, not the last byte.
        pointer += pop_size;
        if ((last_byte >= 0) && (pointer > last_byte)) {
            state = e_complete;
            return IEC_NO_FILE; // no more data?
        }
        if (pointer == 512) {
            if (read_block()) { // also resets pointer.
                log_fault("read_block while streaming");
                return IEC_READ_ERROR;
            } else {
                return IEC_OK;
            }
        }
        break;
    case e_dir:
    case e_partlist:
        pointer += pop_size;
        if ((last_byte >= 0) && (pointer > last_byte)) {
            state = e_complete;
            return IEC_NO_FILE; // no more data?
        }
        if (pointer == prefetch_max) {
            while (read_dir_entry() > 0)
                ;
            return IEC_OK;
        }
        break;
    case e_record:
        pointer += pop_size;
        if (pointer > last_byte) {
            return pop_record();
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
            if (read_block()) { // also resets pointer.
                log_fault("read_block while streaming");
                return IEC_READ_ERROR;
            } else {
                return IEC_OK;
            }
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
            return pop_record();
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
        log_fault("read_block");
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
        if (pointer <= CBMDOS_COMMAND_BUFFER_SIZE) { // one more than open_file() accepts (SI-021)
            buffer[pointer++] = b;
        }
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
                log_fault("push_data block write");
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
        log_fault("push_data on a channel that is not writing");
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
        if (drive->log_every_operation()) {
            log_line("close", NULL, 0, NULL, NULL, 0);
        }
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
                name_to_open.access = e_not_set; // closed: I and UJ closing it again is not a write
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

// The last byte of a record went out. A record that is there is followed by the next
// one; one past the end of the file answers 50 and stays where it is, so reading it
// again answers the same (SI-080, SD fat_file_seek()).
t_channel_retval IecChannel::pop_record(void)
{
    if (recordMissing) {
        drive->get_command_channel()->set_error(ERR_RECORD_NOT_PRESENT, 0, 0);
        pointer = 0;
        prefetch = 0;
        return IEC_OK;
    }
    recordOffset += recordSize;
    if (!read_record(0)) {
        return IEC_OK;
    }
    state = e_complete;
    return IEC_NO_FILE;
}

// A record past the end of the file reads as one byte of 255, and nothing is written for
// it until a record is (SI-080).
void IecChannel::set_missing_record(void)
{
    recordMissing = true;
    memset(buffer, 0, recordSize);
    buffer[0] = 0xFF;
    last_byte = 0;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 1;
    recordDirty = false;
    state = e_record;
}

t_channel_retval IecChannel::read_record(int offset)
{
    FRESULT res = FR_DENIED;
    uint32_t bytes = 0;
    if (f) {
        res = f->read(buffer, recordSize, &bytes);
#if IECDEBUG > 2
        printf("Read record. Expected file position: %d\n", recordOffset);
        dump_hex_relative(buffer, bytes);
#endif
    }
    if (res != FR_OK) {
        printf("%s!\n", FileSystem::get_error_string(res));
        state = e_error;
        return IEC_READ_ERROR;
    }
    if (bytes == 0) {
        set_missing_record();
        return IEC_OK;
    }
    if (bytes < recordSize) { // the last record of a file that ends inside it
        memset(buffer + bytes, 0, recordSize - bytes);
    }
    recordMissing = false;
    last_byte = 0;
    for (int i = recordSize - 1; i > 0; i--) {
        if (buffer[i] != 0) {
            last_byte = i;
            break;
        }
    }

    pointer = offset;
    prefetch = offset;
    prefetch_max = last_byte + 1;
    recordDirty = false;
    state = e_record;
    return IEC_OK;
}

// Before a record is written past the end of the file, the file is filled up to it: a
// last record that ends early is completed, and every record between that and the one
// written is an empty record, whose first byte is 255 as on a 1541.
FRESULT IecChannel::grow_to_record(void)
{
    uint32_t size = f->get_size();
    if (size >= recordOffset) {
        return FR_OK;
    }
    FRESULT res = f->seek(size);
    uint8_t *block = new uint8_t[recordSize];
    uint32_t tr = 0;
    memset(block, 0, recordSize);
    uint32_t partial = (size > dataOffset) ? ((size - dataOffset) % recordSize) : 0;
    if ((res == FR_OK) && partial) {
        res = f->write(block, recordSize - partial, &tr);
        size += recordSize - partial;
    }
    block[0] = 0xFF;
    while ((res == FR_OK) && (size + recordSize <= recordOffset)) {
        res = f->write(block, recordSize, &tr);
        size += recordSize;
    }
    delete[] block;
    return res;
}

t_channel_retval IecChannel::write_record(void)
{
    FRESULT res = FR_DENIED;
    uint32_t bytes;

    if (!recordDirty) {
        return IEC_OK; // do nothing; no data was received
    }
    // A relative file opens for reading whatever the command asks for while the drive is
    // write protected, so the record write is where the protection answers (SI-102). The
    // record is dropped and the channel stays open, because the file can still be read.
    if (drive->is_write_protected()) {
        drive->get_command_channel()->set_error(ERR_WRITE_PROTECT_ON, 0, 0);
        recordDirty = false;
        return IEC_OK;
    }
    if (f) {
        if ((pointer < recordSize) && (pointer >= 0)) {
            memset(buffer + pointer, 0, recordSize - pointer); // fill up with zeros at the end
            if (pointer == 0) {
                buffer[0] = 0xFF;
            }
        }
        res = grow_to_record();
        if (res == FR_OK) {
            res = f->seek(recordOffset);
        }
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

// W-1 and W-0 (SI-102, HD 9-35). Every gate that changes a medium asks the drive, and
// `Suite11-SI102-WriteProtect` sends one command through each of them.
#define REFUSE_WHEN_WRITE_PROTECTED() \
    if (drive->is_write_protected()) { \
        return ERR_WRITE_PROTECT_ON; \
    }

static void iec_path_to_fs_path(mstring &path)
{
    // Path conversion needs to take place, because when the path starts with a /, it
    // should be stripped off, while // means root, which corresponds to starting with
    // / in Ultimate VFS. A left arrow in a path component is a name like any other: it
    // means the parent only in the name position, which do_change_dir() handles (SI-014).
    if (path[0] == '/' && path[1] != '/') {
        mstring stripped(path.c_str() + 1);
        path = stripped;
    }
    path.replace("//", "/");
}

// The disk image formats, by the extension of a name (SD check_imageext()).
typedef enum { e_image_none, e_image_d64, e_image_d71, e_image_d81, e_image_dnp } iec_image_t;

static iec_image_t iec_image_type(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || (strlen(dot) != 4)) {
        return e_image_none;
    }
    char ext[4] = { (char)toupper(dot[1]), (char)toupper(dot[2]), (char)toupper(dot[3]), 0 };
    if (!strcmp(ext, "D64") || !strcmp(ext, "D41")) {
        return e_image_d64;
    }
    if (!strcmp(ext, "D71")) {
        return e_image_d71;
    }
    if (!strcmp(ext, "D81")) {
        return e_image_d81;
    }
    if (!strcmp(ext, "DNP")) {
        return e_image_dnp;
    }
    return e_image_none;
}

// A disk image or a cartridge is stored under exactly its own name, with no type
// extension, so that a PC sees GAME.D81 and not GAME.D81.prg (SI-072, SD should_save_raw()).
static bool iec_name_is_raw(const char *name)
{
    if (iec_image_type(name) != e_image_none) {
        return true;
    }
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return false;
    }
    char ext[5];
    int n = 0;
    for (const char *p = dot + 1; *p && (n < 5); p++) {
        ext[n++] = toupper(*p);
    }
    return ((n == 3) && !strncmp(ext, "CRT", 3)) || ((n == 4) && !strncmp(ext, "TCRT", 4));
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

// The CBM file type an x00 name announces, in the drive's own vocabulary (SI-144). The
// header itself is read by software/filetypes/x00_wrapper.cc, which the C64 loader uses
// as well.
static bool x00_type_of_letter(char letter, filetype_t *type)
{
    switch (letter) {
    case 'P': *type = e_prg; return true;
    case 'S': *type = e_seq; return true;
    case 'U': *type = e_usr; return true;
    case 'R': *type = e_rel; return true;
    }
    return false;
}

static bool x00_type_of_name(const char *path, filetype_t *type)
{
    char letter = 0;
    return x00_name(path, &letter) && x00_type_of_letter(letter, type);
}

static bool x00_type_of_extension(const char *ext, filetype_t *type)
{
    char letter = 0;
    return x00_extension(ext, &letter) && x00_type_of_letter(letter, type);
}

// The CBM name, and the type, of a host file that is a genuine x00 wrapper; false for any
// other file, which is not opened unless its extension is an x00 one.
bool iec_x00_probe(FileManager *fm, const char *path, char *cbm_name, filetype_t *type, uint8_t *record_length)
{
    filetype_t found;
    if (!x00_type_of_name(path, &found) || !x00_read_header(fm, path, cbm_name, record_length)) {
        return false;
    }
    // A trailing run of shifted spaces is padding, as some programs write it (SI-148,
    // SD fatops.c).
    if (cbm_name) {
        int n = strlen(cbm_name);
        while ((n > 0) && ((uint8_t)cbm_name[n - 1] == 0xA0)) {
            cbm_name[--n] = 0;
        }
    }
    if (type) {
        *type = found;
    }
    return true;
}

// The name and type a directory entry has on the bus: the name in its x00 header when it
// is a wrapper, or the host name otherwise. Returns the size of the header, 0 or 26, which
// the entry's size includes.
int iec_entry_name(FileManager *fm, const char *dir_path, FileInfo *info, char *cbm_name, filetype_t& type)
{
    IecPartition::CreateIecName(info, cbm_name, type);
    filetype_t wrapped;
    if ((info->attrib & AM_DIR) || (info->name_format & NAME_FORMAT_CBM) || !dir_path ||
        !x00_type_of_extension(info->extension, &wrapped)) {
        return 0;
    }
    mstring path(dir_path);
    append_path_component(path, info->lfname);
    if (!iec_x00_probe(fm, path.c_str(), cbm_name, &type, NULL)) {
        return 0;
    }
    return X00_HEADER_SIZE;
}

// ConstructPath() names a file of any type with the pattern .???, which fstat() matches.
// This puts the name fstat() found in place of the pattern, because an x00 file is told
// by its real name (SI-144).
static void use_found_name(mstring& path, FileInfo& info)
{
    const char *p = path.c_str();
    const char *slash = strrchr(p, '/');
    mstring found(p, 0, slash ? (int)(slash - p) : -1);
    char entry[80];
    found += (info.name_format & NAME_FORMAT_CBM) ? info.generate_fat_name(entry, sizeof(entry)) : info.lfname;
    path = found;
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
        // A hidden entry is left out of a listing (SI-134) and still answers to its
        // name, as SD passes FLAG_HIDDEN to first_match() and next_match() for every
        // command that names a file.
        if ((info.attrib & AM_VOL) || !info.lfname[0]) {
            continue;
        }
        if ((is_dir && !allow_dirs) || (!is_dir && !allow_files)) {
            continue;
        }

        char iec_name[24];
        filetype_t found_type = e_any;
        iec_entry_name(fm, full_dir, &info, iec_name, found_type);
        if (!is_dir && !iec_file_type_matches(ftype, found_type)) {
            continue;
        }
        if (is_dir && ftype != e_any && ftype != e_folder) {
            continue;
        }
        if (!pattern_match(iec_pattern, iec_name, false)) {
            continue;
        }

        // Inside a disk image a file is found again by its name with the type extension;
        // on a host file system, and for any directory, by the name itself.
        if (!is_dir && (info.name_format & NAME_FORMAT_CBM)) {
            char entry[80];
            actual_name = info.generate_fat_name(entry, sizeof(entry));
        } else {
            actual_name = info.lfname;
        }
        if (out_info) {
            out_info->copyfrom(&info);
        }
        delete dir;
        return FR_OK;
    }

    delete dir;
    return FR_NO_FILE;
}

FRESULT resolve_directory_path(FileManager *fm, IecPartition *partition,
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
    // As many components as the path has, not a fixed number: a deeper path is followed
    // to its end rather than cut off (SI-150).
    int capacity = 1;
    for (const char *p = path.c_str(); *p; p++) {
        capacity += (*p == '/');
    }
    const char **components = new const char *[capacity];
    int count = path.split('/', components, capacity);
    FRESULT result = FR_OK;

    for (int i = 0; i < count; i++) {
        const char *component = components[i]; //requested.getElement(i);
        char fat_component[52];
        const char *direct_component = component;

        if (!strcmp(component, ".") || !strcmp(component, "..")) {
            if (!resolved.cd(component)) { // not possible to move
                result = FR_NO_PATH;
                break;
            }
            continue;
        } 

        petscii_to_fat(component, fat_component, 52);
        // printf("Fat component: %s\n", fat_component);
        direct_component = fat_component;

        // A component with wildcards names the first directory that matches it (SI-012),
        // so it is not tried as a literal name.
        Path direct_path(&resolved);
        if (!iec_name_has_wildcards(component) && direct_path.cd(direct_component)) {
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
                                               e_folder, true, false, true,
                                               actual_name, NULL);
        // printf("Fres = %s. Actual name = %s\n", FileSystem::get_error_string(fres), actual_name.c_str());
        if (fres != FR_OK) {
            result = fres;
            break;
        }
        resolved.cd(actual_name.c_str());
    }
    delete[] components;
    if (result != FR_OK) {
        return result;
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
    if (!name.has_wildcard && direct_relative.cd(fatname)) {
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
                                   e_folder, true, false, true,
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

// The partition types CMD DOS defines, which are the drive's own vocabulary and not
// the file system layer's. Byte 0 of the reply to G-P is one of these codes, and the
// partition directory prints the matching three character name.
typedef enum {
    e_partition_none = 0,
    e_partition_native = 1,
    e_partition_1541 = 2,
    e_partition_1571 = 3,
    e_partition_1581 = 4,
} cbm_partition_type_t;

// Which CMD partition type a file system presents as, decided from the geometry the
// file system reports and from nothing else:
//
//   no track and sector addressing     a directory on the host file system   NAT
//   256 sectors on track 1             a DNP image                           NAT
//   40 sectors on track 1              a D81 image                           81
//   21 sectors on track 1, <= 42 tracks    a D64 image                       41
//   21 sectors on track 1, > 42 tracks     a D71 image                       71
//
// The size of the first zone identifies three of the four layouts on its own. A 1541
// disk and a 1571 disk share it, because a 1571 disk is the 1541 layout twice over, so
// the track count separates those two. 42 tracks is the largest extended 1541 image.
static cbm_partition_type_t cbm_partition_type_of(FileSystem *fs)
{
    if (!fs || !fs->supports_direct_sector_access()) {
        return e_partition_native;
    }
    switch (fs->get_sectors_in_track(1)) {
    case 256: // a CMD native partition addresses 256 sectors on every track
        return e_partition_native;
    case 40:  // a 1581 disk has 40 sectors on every track
        return e_partition_1581;
    case 21:  // the first zone of a 1541 disk, and of each side of a 1571 disk
        return (fs->get_num_tracks() > 42) ? e_partition_1571 : e_partition_1541;
    default:
        return e_partition_native;
    }
}

// The type of a partition follows the file system at its root: a mounted disk image
// reports the drive model it emulates, everything else is native. Only the file
// system of the returned info is used.
static cbm_partition_type_t iec_partition_type(FileManager *fm, IecPartition *prt)
{
    FileInfo info(32);
    if (fm->is_path_valid(prt->GetRootPath(), &info)) {
        return cbm_partition_type_of(info.fs);
    }
    return e_partition_native;
}

// The three character name CMD DOS prints for a partition type. The field is three
// wide, so the shorter names carry their trailing space.
static const char *cbm_partition_type_name(cbm_partition_type_t type)
{
    switch (type) {
    case e_partition_1541: return "41 ";
    case e_partition_1571: return "71 ";
    case e_partition_1581: return "81 ";
    default: return "NAT";
    }
}

// A time stamped directory line has a fixed length: CMD DOS fills the space behind the
// stamp with 0x01 and ends the BASIC line with a zero (SI-139). `used` is the first byte
// behind the stamp and `length` the length the line must have. Returns that length.
static int pad_stamped_entry(uint8_t *buffer, int used, int length)
{
    while (used < (length - 1)) {
        buffer[used++] = 1;
    }
    buffer[length - 1] = 0;
    return length;
}

int IecChannel::read_dir_entry(void)
{
    FileInfo info(INFO_SIZE); // the whole host name, which an x00 file is probed by
    const char *partition_type = NULL;
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
            strncpy(info.lfname, prt->GetName(), info.lfsize);
            info.lfname[info.lfsize - 1] = 0;
            cbm_partition_type_t type = iec_partition_type(fm, prt);
            partition_type = cbm_partition_type_name(type);
            part_idx ++;
            // A type given as $=P:*=N and the like lists only partitions of that type.
            if (name_to_open.dir_opt.partition_types &&
                !(name_to_open.dir_opt.partition_types & (1 << (int)type))) {
                return 1;
            }
        }
    }

    if (fres != FR_OK) {
        if (dir) {
            delete dir;
            dir = NULL;
        }

        pointer = 0;
        prefetch = 0;
        if (state == e_partlist) {
            // A partition directory has no blocks free line (SI-045, issue #890): the
            // BASIC end marker follows the last partition.
            buffer[0] = 0;
            buffer[1] = 0;
            last_byte = 1;
            prefetch_max = 2; // the count of valid bytes, so that pop_data() reaches last_byte
            state = e_dir; // with no directory open, the next call returns -1
            if (drive->log_every_operation()) {
                log_line("listing end", buffer, 2, NULL, NULL, 0);
            }
            return 0;
        }

        buffer[0] = 1;
        buffer[1] = 1;
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
        prefetch_max = 32; // the count of valid bytes, so that pop_data() reaches last_byte
        prefetch = 0;
        state = e_dir; // This causes a -1 to be returned next time this function is called
        if (drive->log_every_operation()) {
            log_line("listing end", buffer, 32, NULL, NULL, 0);
        }
        return 0;
    }
        
    // Skip volume entries
    if (info.attrib & AM_VOL) {
        return 1;
    }
    // A hidden entry is listed only when the filter asks for it (SI-134).
    if ((info.attrib & AM_HID) && !name_to_open.dir_opt.show_hidden) {
        return 1;
    }

    // convert FAT name to CBM name; an x00 wrapper lists as what it wraps (SI-144)
    char cbm_name[24];
    filetype_t ftype = e_any;
    info.size -= iec_entry_name(fm, (state == e_dir) ? dir_path.c_str() : NULL, &info, cbm_name, ftype);
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
    // The low byte of the link carries the size remainder, (size mod 254) + 2, where the
    // size is exact (SI-133). A CBM image counts whole blocks and a partition line has no
    // size, so those keep the plain link.
    bool exact_size = !partition_type && !(info.fs && info.fs->supports_direct_sector_access());
    buffer[0] = exact_size ? (uint8_t)((info.size % 254) + 2) : 1;
    buffer[1] = 1;
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
    memcpy(&buffer[27 - chars], partition_type ? partition_type : types[(int)ftype], 3);
    if (!partition_type && (info.attrib & AM_RDO)) {
        buffer[30 - chars] = '<'; // locked (SI-132)
    }
    // A hidden entry lists only when the filter asks for it, and then it carries an H
    // behind the lock mark, which is the one place a listing reports the attribute
    // (SI-132).
    if (!partition_type && (info.attrib & AM_HID)) {
        buffer[31 - chars] = 'H';
    }
    // An entry in a CBM image whose closed bit is clear is a file a write never finished,
    // and every Commodore drive marks it with a splat in front of the type (SI-132).
    if (!partition_type && info.cbm_filetype && !(info.cbm_filetype & 0x80)) {
        buffer[26 - chars] = '*';
    }

    pointer = 0;
    prefetch = 0;
    switch(name_to_open.dir_opt.timefmt) {
    case e_stamp_none:
        buffer[31] = 0;
        prefetch_max = 32;
        break;
    case e_stamp_long:
        // Three spaces follow the type, so the stamp starts four characters behind it, and
        // the line is a fixed 64 bytes (SI-139). SD createentry() and HD 9-22.
        cbmdos_time(dt, (char *)buffer + 33 - chars, true);
        prefetch_max = pad_stamped_entry(buffer, 33 - chars + 19, 64);
        break;
    case e_stamp_short:
        buffer[28 - chars] = 32; // space out second letter of file type
        cbmdos_time(dt, (char *)buffer + 29 - chars, false);
        prefetch_max = pad_stamped_entry(buffer, 29 - chars + 13, 42);
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
    // The number in front of the header name is the number of partitions (SI-046).
    buffer[4] = (uint8_t)drive->vfs->CountPartitions();
    memcpy(buffer+8, "ULTIMATE HD", 11);
    memcpy(buffer+26, "UL 64", 5);
    return 0;
}

int IecChannel :: setup_directory_read()
{
    DBGIEC("Setup dir read\n");
    char fatname[48];

    // Released and cleared before anything below can fail, so no early return leaves the
    // channel pointing at a freed directory for the next open to free again (CR-5).
    if (dir) {
        delete dir;
        dir = NULL;
    }

    GETPARTITION(name_to_open.file.partition, partition, -1);
    petscii_to_fat(name_to_open.file.filename.c_str(), fatname, 48);

    mstring work;
    mstring relative;
    FRESULT fres = resolve_directory_path(fm, partition, name_to_open.file.path, work, &relative);
    if (fres != FR_OK) {
        drive->set_error(ERR_DIRECTORY_ERROR, drive->vfs->GetTargetPartitionNumber(name_to_open.file.partition), 0);
        state = e_error;
        return -1;
    }

    DBGIECV("Full Path = %s, filename = %s\n", work.c_str(), fatname);

    FileInfo info(40);
    dir_path = work;
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

    FileSystem *fs = info.fs;
    bool labelled = false;
    if (fs->supports_direct_sector_access()) { // might be CBM image!
        dir->get_entry(info); // first one SHOULD be the volume label
        if ((info.attrib & AM_VOL) && (info.name_format & NAME_FORMAT_CBM)) {
            printf("Volume label found: %s\n", info.lfname);
            for (int i = 0; i < 23; i++) {
                buffer[8 + i] = info.lfname[i] & 0x7F;
            }
            buffer[8 + 16] = 0x22;
            labelled = true;
        }
    }

    // The header names the listed directory as it appears in its parent, and the
    // partition only at the root (SI-065, SD fat_getdirlabel()).
    const char *rel = relative.c_str();
    int rel_len = strlen(rel);
    while ((rel_len > 0) && (rel[rel_len - 1] == '/')) {
        rel_len--;
    }
    if (labelled) {
        // the image's own label and id
    } else if (rel_len > 0) {
        int start = rel_len;
        while ((start > 0) && (rel[start - 1] != '/')) {
            start--;
        }
        FileInfo dirinfo(40);
        int n = rel_len - start;
        if (n > dirinfo.lfsize - 1) {
            n = dirinfo.lfsize - 1;
        }
        memcpy(dirinfo.lfname, rel + start, n);
        dirinfo.lfname[n] = 0;
        dirinfo.attrib = AM_DIR;
        char cbm_name[24];
        filetype_t ftype = e_any;
        IecPartition::CreateIecName(&dirinfo, cbm_name, ftype);
        for (int i = 0; (i < 16) && cbm_name[i]; i++) {
            buffer[8 + i] = cbm_name[i];
        }
    } else {
        // The first sixteen characters of the name, as the partition directory and G-P
        // show it (SI-051).
        const char *pp = partition->GetName();
        int pos = 8;
        while ((pos < 24) && (*pp))
            buffer[pos++] = toupper(*(pp++));
    }
    return 0;
}

void print_file(filename_t& file)
{
    printf("P=%d, Path='%s', Filename='%s', Wildcard: %s\n",
        file.partition, file.path.c_str(), file.filename.c_str(), file.has_wildcard?"true":"false" );
}

// The record length of an open relative file and the offset of its first record. An R00
// file has both in its header, which x00_skip_header() has already read (`data_offset` is
// then not 0). Otherwise the record length is the first byte of the file, in this
// firmware's two byte layout or sd2iec's one byte layout (SI-084): a non-zero second byte
// can only be the one byte layout, and otherwise the size leaves 2 mod r over for the one
// and 1 mod r for the other. A record length of 1 leaves both the same and is read as the
// two byte layout. A disk image always presents the two byte layout, whatever its size.
static FRESULT rel_layout(File *f, uint8_t wrapped_length, uint32_t& data_offset, int& length)
{
    length = wrapped_length;
    if (data_offset) {
        return FR_OK;
    }
    uint8_t head[2] = { 0, 0 };
    uint32_t head_bytes = 0;
    FRESULT fres = f->read(head, 2, &head_bytes);
    length = head[0];
    uint32_t size = f->get_size();
    bool in_image = f->get_file_system() && f->get_file_system()->supports_direct_sector_access();
    data_offset = 2;
    if (in_image) {
        // the two byte layout
    } else if ((head_bytes >= 2) && head[1]) {
        data_offset = 1;
    } else if ((length > 1) && ((size % length) != (2 % length)) && ((size % length) == (1 % length))) {
        data_offset = 1;
    }
    return fres;
}

int IecChannel :: setup_file_access()
{
    drive->set_error(0, 0, 0);
    state = e_error;

    // Secondary address 0 reads and 1 writes, whatever the name asks for (SI-070, SD
    // file_open()).
    if (channel == 0) {
        name_to_open.access = e_read;
    } else if (channel == 1) {
        name_to_open.access = e_write;
    } else if (name_to_open.access == e_not_set) {
        name_to_open.access = e_read;
    }
    // A name a file is created under has sixteen characters at most, as on a 1541, so no
    // two new files list under one name (SI-150).
    if (((name_to_open.access == e_write) || name_to_open.record_size) &&
        (name_to_open.file.filename.length() > 16)) {
        name_to_open.file.filename = mstring(name_to_open.file.filename.c_str(), 0, 15);
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
    const char *full_path = NULL;

    // A pattern in a name to write (SI-032). Without @ it is an illegal name. With @ the
    // first match is replaced under its own name if its type is the one asked for; any
    // other type, a relative file, or no match at all answers 64, which is the check the
    // 1541 ROM makes at $D8F5.
    // A name that starts with a shifted space is no name to create (SI-148): 33, or 64 with
    // @, which parse_open() has already answered.
    if ((name_to_open.access == e_write) && ((uint8_t)name_to_open.file.filename.c_str()[0] == 0xA0)) {
        drive->set_error(name_to_open.replace ? ERR_FILE_TYPE_MISMATCH : ERR_SYNTAX_ERROR_NAME, 0, 0);
        return 0;
    }
    if ((name_to_open.access == e_write) && name_to_open.file.has_wildcard) {
        if (!name_to_open.replace) {
            drive->set_error(ERR_SYNTAX_ERROR_NAME, 0, 0);
            return 0;
        }
        GETPARTITION(name_to_open.file.partition, partition, 0);
        FileInfo matched(INFO_SIZE);
        filetype_t found = e_any;
        FRESULT fres = resolve_existing_iec_path(fm, partition, name_to_open.file, e_any,
                                                 false, true, true, work, &matched);
        char cbm_name[24];
        if ((fres == FR_OK) && !iec_x00_probe(fm, work.c_str(), cbm_name, &found, NULL)) {
            IecPartition::CreateIecName(&matched, cbm_name, found);
        }
        if ((fres != FR_OK) || (found != name_to_open.filetype) || (found == e_rel)) {
            drive->set_error(ERR_FILE_TYPE_MISMATCH, 0, 0);
            return 0;
        }
        full_path = work.c_str();
    } else {
        full_path = ConstructPath(work, name_to_open.file, name_to_open.filetype, name_to_open.access );
    }
    if (!full_path) {
        drive->set_error(ERR_PARTITION_ERROR, drive->vfs->GetTargetPartitionNumber(name_to_open.file.partition), 0);
        return 0;
    }

    // Existing REL files also open by name alone (for example the E.DATA editor).
    // Resolve the type before choosing access flags and setting up record I/O.
    if (name_to_open.filetype == e_any) {
        FileInfo info(INFO_SIZE);
        FRESULT fres = fm->fstat(full_path, info);
        if (fres == FR_NO_FILE) {
            GETPARTITION(name_to_open.file.partition, partition, 0);
            fres = resolve_existing_iec_path(fm, partition, name_to_open.file, e_any,
                                             false, true, true, work, &info);
        } else if (fres == FR_OK) {
            use_found_name(work, info);
            // An x00 file answers to the name in its header, not to its host name (SI-144).
            char header_name[24];
            if (iec_x00_probe(fm, work.c_str(), header_name, NULL, NULL) &&
                strcmp(header_name, name_to_open.file.filename.c_str())) {
                GETPARTITION(name_to_open.file.partition, partition, 0);
                fres = resolve_existing_iec_path(fm, partition, name_to_open.file, e_any,
                                                 false, true, true, work, &info);
            }
        }
        full_path = work.c_str();
        if (fres != FR_OK) {
            drive->set_error_fres(fres);
            return 0;
        }
        char cbm_name[24];
        if (!iec_x00_probe(fm, full_path, cbm_name, &name_to_open.filetype, NULL)) {
            IecPartition::CreateIecName(&info, cbm_name, name_to_open.filetype);
        }
    }

    // A name belongs to one entry, whatever its type (SI-035, SD file_open()). Without @ an
    // entry of that name answers 63. With @, one of another type answers 64, as the 1541 ROM
    // does at $D8F5, and an x00 file of the same type makes way for the new file.
    if ((name_to_open.access == e_write) && !name_to_open.file.has_wildcard) {
        GETPARTITION(name_to_open.file.partition, partition, 0);
        FileInfo existing(INFO_SIZE);
        mstring found_path;
        if ((fm->fstat(full_path, existing) == FR_NO_FILE) &&
            (resolve_existing_iec_path(fm, partition, name_to_open.file, e_any,
                                       false, true, false, found_path, &existing) == FR_OK)) {
            char cbm_name[24];
            filetype_t found_type = e_any;
            bool wrapped = iec_x00_probe(fm, found_path.c_str(), cbm_name, &found_type, NULL);
            if (!wrapped) {
                IecPartition::CreateIecName(&existing, cbm_name, found_type);
            }
            if (!name_to_open.replace) {
                drive->set_error(ERR_FILE_EXISTS, 0, 0);
                return 0;
            }
            if (found_type != name_to_open.filetype) {
                drive->set_error(ERR_FILE_TYPE_MISMATCH, 0, 0);
                return 0;
            }
            // The write protect refuses the open below, and must leave the old file.
            if (wrapped && !drive->is_write_protected()) {
                fm->delete_file(found_path.c_str());
            }
        }
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
        // FA_OPEN_FROM_CBM keeps FileInCBM::open() from wrapping a GEOS file in a CVT
        // header, which belongs to the file browser and not to the bus (SI-149). 3.14d
        // passed it here; commit 76d887bd rewrote this open and dropped it.
        flags = FA_READ | FA_OPEN_FROM_CBM;
    }
    if (name_to_open.filetype == e_rel) {
        flags |= ( FA_READ | FA_WRITE );
        if (name_to_open.record_size != 0) {
            flags |= FA_OPEN_ALWAYS;
        }
    }

    // A write protected drive opens nothing that would create or change a file, and
    // opens a relative file for reading only (SI-102).
    if (drive->is_write_protected()) {
        if (name_to_open.filetype == e_rel) {
            flags = FA_READ | (flags & FA_OPEN_FROM_CBM);
        } else if (flags & FA_WRITE) {
            drive->set_error(ERR_WRITE_PROTECT_ON, 0, 0);
            return 0;
        }
    }

    DBGIECV("Setup File Access %s %02x\n", full_path, flags);

    // A relative file that already exists is opened by its CBM name, which finds it in an
    // x00 wrapper too (SI-146), rather than created again beside it.
    if ((name_to_open.filetype == e_rel) && name_to_open.record_size && !name_to_open.file.has_wildcard) {
        GETPARTITION(name_to_open.file.partition, partition, 0);
        mstring existing;
        if (resolve_existing_iec_path(fm, partition, name_to_open.file, e_rel,
                                      false, true, false, existing) == FR_OK) {
            work = existing;
            full_path = work.c_str();
        }
    }

    FRESULT fres = fm->fopen(full_path, flags, &f);
    if ((fres == FR_NO_FILE) && ((name_to_open.access == e_read) || (name_to_open.access == e_append))) {
        GETPARTITION(name_to_open.file.partition, partition, 0);
        fres = open_by_rendered_iec_name(fm, partition, name_to_open.file,
                                         name_to_open.filetype, flags, &f, work);
        if (fres == FR_OK) {
            full_path = work.c_str();
            DBGIECV("Directory-assisted open resolved to %s\n", full_path);
        } else if ((fres == FR_NO_FILE) && (name_to_open.filetype != e_rel)) {
            // An existing relative file opened as another type is a type mismatch (SI-035).
            mstring rel;
            if (resolve_existing_iec_path(fm, partition, name_to_open.file, e_rel,
                                          false, true, true, rel) == FR_OK) {
                drive->set_error(ERR_FILE_TYPE_MISMATCH, 0, 0);
                return 0;
            }
        }
    }
    if (fres != FR_OK) {
        // A relative file that is not there would be created, which W-1 refuses (SI-102).
        if ((fres == FR_NO_FILE) && (name_to_open.filetype == e_rel) && name_to_open.record_size &&
            drive->is_write_protected()) {
            drive->set_error(ERR_WRITE_PROTECT_ON, 0, 0);
            return 0;
        }
        drive->set_error_fres(fres);
        return 0;
    }

    last_byte = -1;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 512;
    state = e_file;

    // An existing file with an x00 name is read past its header when it has one (SI-144).
    uint8_t wrapped_length = 0;
    if (name_to_open.access != e_write) {
        dataOffset = x00_skip_header(f, full_path, &wrapped_length);
    }

    if (name_to_open.filetype == e_rel) {
        uint32_t tr;
        if (!f->get_size()) { // the file must be newly created, because its size is 0.
            uint16_t wrd = name_to_open.record_size;
            fres = f->write(&wrd, 2, &tr);
            drive->set_error_fres(fres);
            if (fres == FR_OK) {
                recordSize = name_to_open.record_size;
                dataOffset = 2;
                recordOffset = 2; // First record follows the REL header, also on a reused channel.
                set_missing_record(); // a new file has no first record until one is written
            }
        } else { // file already exists
            int length;
            fres = rel_layout(f, wrapped_length, dataOffset, length);
            if ((fres != FR_OK) || (length == 0)) {
                DBGIECV("WARNING: Illegal record size in .rel file...Is it a REL file at all? (%d)\n", length);
                state = e_error;
                drive->set_error(ERR_RECORD_NOT_PRESENT, 0, 0);
                return ERR_RECORD_NOT_PRESENT;
            }
            recordSize = (uint8_t) length;
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

// "#" opens a 256 byte buffer with its pointer at byte 1 (SI-090, SD README). The channel
// keeps the partition that is current now (SI-093).
int IecChannel::setup_buffer_access(void)
{
    // A buffer here is 256 bytes, the sector size of every disk image this drive serves,
    // so a chain of more than one cannot be given out (SI-090). The chain form asking for
    // one is the same buffer with its pointer at byte 0.
    if (name_to_open.buffers > 1) {
        state = e_error;
        drive->set_error(ERR_NO_CHANNEL, 0, 0);
        return -1;
    }
    last_byte = 255;
    pointer = (name_to_open.buffers == 1) ? 0 : 1;
    prefetch = pointer;
    prefetch_max = 256;
    buffer_partition = drive->vfs->GetTargetPartitionNumber(0);
    state = e_buffer;

    return 0;
}

int IecChannel::open_file(void)  // name should be in buffer
{
    // The name as the bus delivered it, kept for the failure log: setting up the stream
    // reads the first block of a file into the same buffer.
    static uint8_t raw_name[CBMDOS_COMMAND_BUFFER_SIZE];
    int raw_len = (pointer > (int)sizeof(raw_name)) ? (int)sizeof(raw_name) : pointer;
    memcpy(raw_name, buffer, raw_len);
    fault_logged = false;
    buffer[pointer] = 0; // string terminator
    DBGIECV("Open file. Raw Filename = '%s'\n", buffer);
    // A name that fills the buffer is refused, as a command is, rather than opened under
    // its first bytes (SI-022).
    int parse_err = (pointer >= CBMDOS_COMMAND_BUFFER_SIZE) ? ERR_SYNTAX_ERROR_CMDLENGTH :
                    parse_open((const char *)buffer, name_to_open);
    if (parse_err) {
        state = e_error;
        drive->set_error(parse_err, 0, 0);
        log_line("open failed", raw_name, raw_len, NULL, NULL, 0);
        return -1;
    }

    recordSize = 0;
    dataOffset = 0;

    int result = -1;
    switch(name_to_open.dir_opt.stream) {
    case e_stream_buffer:
        result = setup_buffer_access();
        break;
    case e_stream_dir:
        result = setup_directory_read();
        break;
    case e_stream_partitions:
        result = setup_partition_read();
        break;
    case e_stream_file:
        result = setup_file_access();
        break;
    default: // file
        DBGIEC("Unknown stream mode\n");
        break;
    }
    if ((state == e_error) && drive_failed()) {
        log_line("open failed", raw_name, raw_len, NULL, NULL, 0);
    } else if (drive->log_every_operation()) {
        // The host file the name reached, or the first bytes of a listing.
        if (f) {
            const char *host = f->get_path();
            log_line("open", raw_name, raw_len, "host", (const uint8_t *)host, host ? strlen(host) : 0);
        } else if ((state == e_dir) || (state == e_partlist)) {
            log_line("open", raw_name, raw_len, "data", buffer, (prefetch_max < 32) ? prefetch_max : 32);
        } else {
            log_line("open", raw_name, raw_len, NULL, NULL, 0);
        }
    }
    return result;
}

// Closes the file, and releases the directory of a listing the host stopped reading
// (CR-4), which otherwise stayed allocated until the channel listed again.
int IecChannel::close_file(void) // file should be open
{
    if (f)
        fm->fclose(f);
    f = NULL;
    if (dir) {
        delete dir;
        dir = NULL;
    }
    state = e_idle;
    return 0;
}

int IecChannel::ext_open_file(const char *name)
{
    close_file(); // as an OPEN from the bus closes what the channel had open
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
    const uint32_t c_header = dataOffset; // the record length in front of the records (SI-084)
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

    uint32_t targetPosition = c_header + (recordNumber * recordSize);
    recordOffset = targetPosition;
    int err = ERR_ALL_OK;
    if (offset > recordSize - 1) {
        offset = recordSize - 1;
        err = ERR_OVERFLOW_IN_RECORD;
    }
    // A record past the end is not there, which is not an error when the next thing is
    // to write it; the write grows the file (SI-080). The file position is left alone,
    // because a seek past the end of a writable file would extend it.
    if (f->get_size() < targetPosition + recordSize) {
        set_missing_record();
        return err ? err : ERR_RECORD_NOT_PRESENT;
    }
    FRESULT fres = f->seek(targetPosition);
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
        return 0;
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
    if (wr_pointer < CBMDOS_COMMAND_BUFFER_SIZE) {
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
    // A name with a disk image extension is written under exactly that name when it is a
    // PRG, and gets its type extension like any other name otherwise (SI-072, SD
    // fat_open(), `type == TYPE_PRG && should_save_raw()`).
    bool raw = iec_name_is_raw(name.filename.c_str()) &&
               ((acc != e_write) || (ftype == e_prg) || (ftype == e_folder));
    if (((acc == e_read) || (ftype != e_any)) && !raw) { // .??? is for reads only
        add_extension(fatname, ext, 48);
    }

    FRESULT dir_fres = resolve_directory_path(fm, partition, name.path, work);
    if (dir_fres != FR_OK) {
        return NULL;
    }
    append_path_component(work, fatname);
    return work.c_str();
}


// The direct access channel a block command names, or NULL with 70 set when the channel
// was not opened with #.
IecChannel *IecCommandChannel::buffer_channel(int chan)
{
    if ((chan < 0) || (chan > 14) || (drive->get_data_channel(chan)->state != e_buffer)) {
        drive->set_error(ERR_NO_CHANNEL, 0, 0);
        return NULL;
    }
    return drive->get_data_channel(chan);
}

// The result of a sector access, as the error channel reports it.
void IecCommandChannel::sector_error(FRESULT fres, int track, int sector)
{
    if (fres == FR_DENIED) {
        drive->set_error(ERR_DENIED, track, sector);
    } else if (fres == FR_INVALID_PARAMETER) { // outside the disk (SI-036)
        drive->set_error(ERR_ILLEGAL_TRACK_SECTOR, track, sector);
    } else {
        drive->set_error_fres(fres);
    }
}

// U1 and B-R. The partition parameter is ignored: the channel reads from the partition that
// was current when it was opened (SI-093). U1 makes all 256 bytes readable from byte 0;
// B-R takes the count from byte 0 and starts at byte 1 (SI-094).
int IecCommandChannel :: do_block_read(int chan, int part, int track, int sector, bool length_byte)
{
    IecChannel *channel = buffer_channel(chan);
    if (!channel) {
        return 0;
    }
    GETPARTITION(channel->buffer_partition, partition, -1);
    Path path(partition->GetFullPath());
    FRESULT fres = fm->fs_read_sector(&path, channel->buffer, track, sector);
    sector_error(fres, track, sector);
    channel->pointer = length_byte ? 1 : 0;
    channel->last_byte = length_byte ? channel->buffer[0] : 255;
    channel->reset_prefetch();
    state = e_idle;
    return 0;
}

// U2 and B-W, to the partition the channel was opened on; B-W first stores the buffer
// pointer minus one in byte 0 (SI-094).
int IecCommandChannel::do_block_write(int chan, int part, int track, int sector, bool length_byte)
{
    REFUSE_WHEN_WRITE_PROTECTED();
    IecChannel *channel = buffer_channel(chan);
    if (!channel) {
        return 0;
    }
    GETPARTITION(channel->buffer_partition, partition, -1);
    if (length_byte) {
        channel->buffer[0] = (uint8_t)(channel->pointer - 1);
    }
    Path path(partition->GetFullPath());
    FRESULT fres = fm->fs_write_sector(&path, channel->buffer, track, sector);
    sector_error(fres, track, sector);
    state = e_idle;
    return 0;
}

// B-A and B-F have no channel, so they act on the current partition; the partition
// parameter is ignored like that of the other direct access commands (SI-013).
int IecCommandChannel::do_block_allocate(int part, int track, int sector, bool alloc)
{
    REFUSE_WHEN_WRITE_PROTECTED();
    GETPARTITION(0, partition, -1);
    Path path(partition->GetFullPath());
    FRESULT fres = fm->fs_allocate_sector(&path, track, sector, alloc);
    if (fres == FR_EXIST) { // the block was allocated; track and sector name the next free one
        drive->set_error(ERR_NO_BLOCK, track, sector);
    } else {
        sector_error(fres, track, sector);
    }
    state = e_idle;
    return 0;
}

// B-P on a buffer channel.
int IecCommandChannel :: do_buffer_position(int chan, int pos)
{
    IecChannel *channel = buffer_channel(chan);
    if (!channel) {
        return 0;
    }
    // A buffer is 256 bytes, so a position a high byte puts past its end names no byte
    // this drive can give out (SI-090, SI-092). P passes four bytes, so the top one can
    // make the number negative.
    if ((pos < 0) || (pos > 255)) {
        return ERR_SYNTAX_ERROR_GEN;
    }
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

    // The left arrow, PETSCII 0x5F, means the parent directory in the name position:
    // directly after the command word, as in "CD_", which the parser returns as a path
    // of just the arrow, and behind a colon, as in "CD:_" or "CD/SUB/:_". Between
    // slashes it is a directory name, so "CD/_" enters a directory called _ (SI-014,
    // SI-015). Both name forms become the path component .., which is always the parent.
    if ((dest.path == "_") && (dest.filename.length() == 0)) {
        dest.path = "";
        dest.filename = "_";
    }
    if (dest.filename == "_") {
        dest.filename = "";
        if (dest.path.length() && (dest.path[-1] != '/')) {
            dest.path += "/";
        }
        dest.path += "..";
    }

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
    REFUSE_WHEN_WRITE_PROTECTED();
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
    REFUSE_WHEN_WRITE_PROTECTED();
    mstring work;
    const char *fullpath = ConstructPath(work, dest, e_folder, e_read);
    if (fullpath) {
        DBGIECV("Directory to remove: %s\n", fullpath);
        // RD removes a directory and nothing else: a host file reached by the same name,
        // a disk image or a file without an extension, is not one (SI-063).
        FileInfo info(INFO_SIZE);
        FRESULT fres = fm->fstat(fullpath, info);
        if (fres == FR_OK) {
            fres = (info.attrib & AM_DIR) ? fm->delete_file(fullpath) : FR_NO_FILE;
        }
        if (fres == FR_NO_FILE || fres == FR_NO_PATH) {
            // A pattern names the first directory that matches it; its host name escapes
            // the wildcards, so only the directory scan can find it (SI-142).
            GETPARTITION(dest.partition, partition, 0);
            fres = resolve_existing_iec_path(fm, partition, dest, e_folder,
                                             true, false, true, work);
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
    REFUSE_WHEN_WRITE_PROTECTED();
    // Curiously, an original drive takes the file type from the FIRST file it copies.
    // So in order to know the destination extension, we'd need to first open the first file
    // using wildcards and then see what file was opened.
    mstring work;
    FileInfo info(INFO_SIZE);
    const char *source1 = ConstructPath(work, sources[0], e_any, e_read);
    FRESULT fres = source1 ? fm->fstat(source1, info) : FR_NO_PATH;
    if (fres == FR_OK) {
        use_found_name(work, info);
    } else {
        GETPARTITION(sources[0].partition, partition, 0);
        fres = resolve_existing_iec_path(fm, partition, sources[0], e_any,
                                         false, true, sources[0].has_wildcard,
                                         work, &info);
    }
    source1 = work.c_str();
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
        return ERR_FILE_NOT_FOUND;
    }
    // convert FAT name to CBM name, or take it from an x00 header (SI-144)
    char cbm_name[24];
    filetype_t ftype = e_any;
    if (!iec_x00_probe(fm, source1, cbm_name, &ftype, NULL)) {
        IecPartition::CreateIecName(&info, cbm_name, ftype);
    }
    // ftype is now set to the type of the first original file.

    // A relative file copies its records under one record length, so it goes with relative
    // files only, as SD parse_copy() has it (SI-075). Every source is checked before the
    // target exists, so that a refused copy leaves nothing behind.
    for (int i = 1; i < n; i++) {
        mstring other;
        FileInfo other_info(INFO_SIZE);
        const char *path = ConstructPath(other, sources[i], e_any, e_read);
        FRESULT found = path ? fm->fstat(path, other_info) : FR_NO_PATH;
        if (found == FR_OK) {
            use_found_name(other, other_info);
        } else {
            IecPartition *partition = drive->vfs->GetPartition(sources[i].partition);
            found = partition ? resolve_existing_iec_path(fm, partition, sources[i], e_any, false, true,
                                                          sources[i].has_wildcard, other, &other_info)
                              : FR_NO_PATH;
        }
        if (found != FR_OK) {
            continue; // reported where the copy reaches it
        }
        char other_name[24];
        filetype_t other_type = e_any;
        if (!iec_x00_probe(fm, other.c_str(), other_name, &other_type, NULL)) {
            IecPartition::CreateIecName(&other_info, other_name, other_type);
        }
        if ((other_type == e_rel) != (ftype == e_rel)) {
            return ERR_FILE_TYPE_MISMATCH;
        }
    }

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
    int rel_length = 0;

    for(int i=0;i<n;i++) {
        const char *frompath = ConstructPath(work, sources[i], e_any, e_read);
        if (frompath) {
            DBGIECV("  %d. %s\n", i, frompath);
        } else {
            DBGIECV("  %d. unresolved by canonical path\n", i);
        }
        FileInfo found(INFO_SIZE);
        fres = frompath ? fm->fstat(frompath, found) : FR_NO_PATH;
        if (fres == FR_OK) {
            use_found_name(work, found);
            frompath = work.c_str();
            fres = fm->fopen(frompath, FA_READ, &fi);
        }
        if (fres != FR_OK) {
            // Not GETPARTITION(), whose return would leave the target open and the copy
            // buffer allocated.
            IecPartition *partition = drive->vfs->GetPartition(sources[i].partition);
            if (!partition) {
                set_error(ERR_PARTITION_ERROR, drive->vfs->GetTargetPartitionNumber(sources[i].partition));
                break;
            }
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
        uint8_t wrapped_length = 0;
        uint32_t data_offset = x00_skip_header(fi, frompath, &wrapped_length); // data, not header
        if (ftype == e_rel) {
            // The target has one record length, written once; each source adds its records.
            int length = 0;
            fres = rel_layout(fi, wrapped_length, data_offset, length);
            if ((fres == FR_OK) && (i == 0)) {
                rel_length = length;
                uint16_t wrd = (uint16_t)length;
                uint32_t written;
                fres = fo->write(&wrd, 2, &written);
            }
            if ((fres == FR_OK) && ((length == 0) || (length != rel_length))) {
                set_error(ERR_FILE_TYPE_MISMATCH);
                fm->fclose(fi);
                break;
            }
            if (fres == FR_OK) {
                fres = fi->seek(data_offset);
            }
            if (fres != FR_OK) {
                drive->set_error_fres(fres);
                fm->fclose(fi);
                break;
            }
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

// I: there is no medium to read in again, so what is left of initialising is what
// sd2iec does, closing the data channels a program left open (SI-053). A channel
// written to is closed the way a CLOSE from the host closes it, so its data is kept.
int IecCommandChannel::do_initialize_buffers()
{
    for (int i = 0; i < 15; i++) {
        drive->get_data_channel(i)->push_command(0xE0);
    }
    drive->set_error(ERR_ALL_OK, 0, 0);
    return 0;
}

// UJ and U+shifted J (SI-103). Both close the data channels as I does, and U+shifted J
// also returns every partition to its root and selects partition 1, which is what
// IecDrive::reset() does to the partitions. Neither calls IecDrive::reset(): that reaches
// IecInterface::configure(), which holds the IEC processor in reset while the host is
// still addressing this drive.
int IecCommandChannel::do_reset(bool cold)
{
    do_initialize_buffers();
    drive->set_clock_offset(0); // the drive's clock is the system clock again (SI-120)
    if (cold) {
        for (int i = 1; i < MAX_PARTITIONS; i++) {
            IecPartition *p = drive->vfs->GetPartition(i);
            if (p) {
                p->cd("/");
            }
        }
        drive->vfs->SetCurrentPartition(1);
    }
    return ERR_DOS;
}

// N[n][path]:name[,id] (SI-071). This drive has no medium of its own to format, so like
// sd2iec it creates a disk image, or formats one that exists, chosen by the extension:
// .D64 or .D41, .D71, .D81, and .DNP, whose id is a three digit track count and which is
// created but not formatted. A name without an image extension gets .D64, and then an
// existing file is left alone, as an existing DNP always is. A new image needs an id.
int IecCommandChannel::do_format(filename_t& dest, const char *id)
{
    REFUSE_WHEN_WRITE_PROTECTED();
    GETPARTITION(dest.partition, partition, -1);
    mstring dir;
    FRESULT fres = resolve_directory_path(fm, partition, dest.path, dir);
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
        return 0;
    }

    // Inside a disk image, N formats that image rather than creating an image inside it,
    // which is what a program formatting its disk asks for (SI-071, SD parse_new()). The
    // format goes through the file system that has the image open, so the block map and
    // the directory it holds are the ones that are rewritten.
    FileInfo dir_info(4);
    Directory *probe = NULL;
    if (fm->open_directory(dir.c_str(), &probe, &dir_info) == FR_OK) {
        delete probe;
        if (dir_info.fs && dir_info.fs->supports_direct_sector_access()) {
            // A file open on the image would keep reading and writing blocks the format
            // has handed back, so it is the user's file that decides, not the command.
            for (int i = 0; i < 15; i++) {
                IecChannel *ch = drive->get_data_channel(i);
                if (ch && ch->f && (ch->f->get_file_system() == dir_info.fs)) {
                    return ERR_WRITE_FILE_OPEN;
                }
            }
            mstring label(dest.filename.c_str());
            if (*id) {
                label += ",";
                label += id;
            }
            fres = dir_info.fs->format(label.c_str());
            if (fres != FR_OK) {
                drive->set_error_fres(fres);
                return 0;
            }
            // A subdirectory inside the image is gone with the format, so the partition
            // falls back to the deepest directory that still exists.
            while (!fm->is_path_valid(partition->GetFullPath())) {
                if (!partition->cd("..")) {
                    break;
                }
            }
            set_error(ERR_ALL_OK);
            return 0;
        }
    }

    // A label of up to 16 characters and a four character extension.
    char name[24];
    char label[24];
    strncpy(name, dest.filename.c_str(), 20);
    name[20] = 0;
    iec_image_t kind = iec_image_type(name);
    bool extension_given = (kind != e_image_none);
    if (!extension_given) {
        kind = e_image_d64;
        name[16] = 0;
        strcat(name, ".D64");
    }
    strcpy(label, name);
    label[strrchr(label, '.') - label] = 0;

    int idlen = strlen(id);
    if (kind == e_image_dnp) {
        if (idlen && ((idlen != 3) || !isdigit(id[0]) || !isdigit(id[1]) || !isdigit(id[2]))) {
            return ERR_SYNTAX_ERROR_GEN;
        }
    } else if (idlen && ((idlen < 2) || (idlen > 3))) {
        return ERR_SYNTAX_ERROR_GEN;
    }

    char host[52];
    petscii_to_fat(name, host, sizeof(host));
    mstring full(dir.c_str());
    append_path_component(full, host);

    File *f = NULL;
    FileInfo existing(8);
    if (fm->fstat(full.c_str(), existing) == FR_OK) {
        if (!extension_given || (kind == e_image_dnp)) {
            return ERR_FILE_EXISTS;
        }
        fres = fm->fopen(full.c_str(), FA_READ | FA_WRITE, &f);
    } else {
        if (!idlen) {
            return ERR_SYNTAX_ERROR_GEN;
        }
        uint32_t size = 0;
        switch (kind) {
        case e_image_d64: size = 174848; break;
        case e_image_d71: size = 349696; break;
        case e_image_d81: size = 819200; break;
        default: size = 65536 * (uint32_t)atoi(id); break;
        }
        if (size == 0) {
            return ERR_FILE_NOT_FOUND;
        }
        Path dir_path(dir.c_str());
        uint32_t free_clusters = 0, cluster_size = 0;
        if ((fm->get_free(&dir_path, free_clusters, cluster_size) == FR_OK) &&
            (((uint64_t)free_clusters * cluster_size) < size)) {
            return ERR_DISK_FULL;
        }
        fres = fm->fopen(full.c_str(), FA_CREATE_NEW | FA_READ | FA_WRITE, &f);
        if ((fres == FR_OK) && (kind == e_image_dnp)) {
            fres = f->seek(size); // the space is claimed; the image is not formatted
        } else if (fres == FR_OK) {
            uint8_t empty[256];
            memset(empty, 0, sizeof(empty));
            for (uint32_t done = 0; (done < size) && (fres == FR_OK); done += sizeof(empty)) {
                uint32_t transferred;
                fres = f->write(empty, sizeof(empty), &transferred);
            }
        }
    }

    if ((fres == FR_OK) && (kind != e_image_dnp)) {
        // The file system stack borrows f, so it goes out of scope before f is closed.
        char volume[32];
        snprintf(volume, sizeof(volume), "%s,%s", label, id);
        BlockDevice_File blk(f, 256);
        Partition prt(&blk, 0, 0, 0);
        switch (kind) {
        case e_image_d71: { FileSystemD71 fs(&prt, true); fres = fs.format(volume); break; }
        case e_image_d81: { FileSystemD81 fs(&prt, true); fres = fs.format(volume); break; }
        default:          { FileSystemD64 fs(&prt, true); fres = fs.format(volume); break; }
        }
    }
    if (f) {
        fm->fclose(f);
    }
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
    }
    return 0;
}

int IecCommandChannel::do_rename(filename_t &src, filename_t &dest)
{
    REFUSE_WHEN_WRITE_PROTECTED();
    mstring works, workd;
    const char *src_path = ConstructPath(works, src, e_any, e_read);
    FileInfo info(INFO_SIZE);
    FRESULT fres = src_path ? fm->fstat(src_path, info) : FR_NO_PATH;
    if (fres == FR_OK) {
        use_found_name(works, info);
    } else {
        GETPARTITION(src.partition, partition, 0);
        // A subdirectory is renamed under its own name, so directories are matched
        // as well as files (SI-074).
        fres = resolve_existing_iec_path(fm, partition, src, e_any,
                                         true, true, src.has_wildcard,
                                         works, &info);
    }
    src_path = works.c_str();
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
        return 0;
    }
    // convert FAT name to CBM name, or take it from an x00 header (SI-144)
    char cbm_name[24];
    filetype_t ftype = e_any;
    bool wrapped = iec_x00_probe(fm, src_path, cbm_name, &ftype, NULL);
    if (!wrapped) {
        IecPartition::CreateIecName(&info, cbm_name, ftype);
    }
    // ftype is now set to the type of the original file.

    // A directory takes its name without a type extension, so a FAT host would drop a
    // trailing dot or space from it and the directory could not be found again (SI-141).
    int new_len = dest.filename.length();
    if ((info.attrib & AM_DIR) && new_len &&
        ((dest.filename.c_str()[new_len - 1] == '.') || (dest.filename.c_str()[new_len - 1] == ' '))) {
        return ERR_SYNTAX_ERROR_NAME;
    }

    // The new name must not be taken by an entry of any type, unless it is the old name
    // spelled in another case (SI-074).
    GETPARTITION(dest.partition, dest_partition, 0);
    mstring dest_dir;
    if (resolve_directory_path(fm, dest_partition, dest.path, dest_dir) != FR_OK) {
        drive->set_error(ERR_DIRECTORY_ERROR, dest_partition->GetPartitionNumber(), 0);
        return 0;
    }
    // Only a rename in place can keep the name; a move meets the names of another
    // directory.
    const char *slash = strrchr(src_path, '/');
    int src_dir_len = slash ? (int)(slash - src_path) : 0;
    int dest_dir_len = dest_dir.length();
    while ((dest_dir_len > 0) && (dest_dir.c_str()[dest_dir_len - 1] == '/')) {
        dest_dir_len--;
    }
    bool in_place = (src_dir_len == dest_dir_len) && !strncasecmp(src_path, dest_dir.c_str(), src_dir_len);
    if (!in_place || (strcasecmp(cbm_name, dest.filename.c_str()) != 0)) {
        mstring taken;
        if (find_rendered_iec_child(fm, dest_dir.c_str(), dest.filename.c_str(), e_any,
                                    true, true, false, taken, NULL) == FR_OK) {
            return ERR_FILE_EXISTS;
        }
    }

    // An x00 file takes the new name in its header and in its host name, so the two agree
    // and the file browser, a PC and the bus all show the same name (SDM fat_rename()).
    if (wrapped) {
        fres = x00_rename(fm, src_path, dest_dir.c_str(), dest.filename.c_str());
        if (fres != FR_OK) {
            drive->set_error_fres(fres);
        }
        return 0;
    }

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

// Scratches every file in a directory whose CBM name matches a pattern, and returns how
// many went. The names are collected before anything is deleted, so the directory is
// not changed while it is being read. The host name of a pattern escapes its wildcards
// (SI-142), so the pattern is matched here rather than by the host file system.
// `protected_medium` is set when the medium refused a delete as write protected.
static int scratch_matching(FileManager *fm, const char *dir_path, const char *pattern,
                            bool *protected_medium)
{
    Directory *dir = NULL;
    if (fm->open_directory(dir_path, &dir) != FR_OK) {
        return 0;
    }
    IndexedList<mstring *> victims(8, NULL);
    IndexedList<mstring *> locked(4, NULL);
    FileInfo info(INFO_SIZE);
    while (dir->get_entry(info) == FR_OK) {
        if ((info.attrib & (AM_DIR | AM_VOL)) || !info.lfname[0]) {
            continue;
        }
        // Inside a disk image an entry is found by its name with the type extension,
        // which generate_fat_name() adds; on a host file system it is the name itself.
        char entry[80];
        mstring *full = new mstring(dir_path);
        append_path_component(*full, info.generate_fat_name(entry, sizeof(entry)));
        // Locked files are not scratched (SI-076). A delete removes the first entry of a
        // name, so an entry that follows a locked one of the same name, which a damaged
        // disk image can have, is not scratched either.
        bool behind_locked = false;
        for (int i = 0; (i < locked.get_elements()) && !behind_locked; i++) {
            behind_locked = (strcmp(locked[i]->c_str(), full->c_str()) == 0);
        }
        char cbm_name[24];
        filetype_t ftype = e_any;
        iec_entry_name(fm, dir_path, &info, cbm_name, ftype);
        if (info.attrib & AM_RDO) {
            locked.append(full);
        } else if (!behind_locked && pattern_match(pattern, cbm_name, false)) {
            victims.append(full);
        } else {
            delete full;
        }
    }
    delete dir;

    int scratched = 0;
    for (int i = 0; i < victims.get_elements(); i++) {
        FRESULT fres = fm->delete_file(victims[i]->c_str());
        if (fres == FR_OK) {
            scratched++;
        } else if (fres == FR_WRITE_PROTECTED) {
            *protected_medium = true;
        }
        delete victims[i];
    }
    for (int i = 0; i < locked.get_elements(); i++) {
        delete locked[i];
    }
    return scratched;
}

// Every name, with or without a pattern, is scratched through the directory scan, which
// removes each unlocked entry of that name once and then stops. Deleting the name until
// the file system refused, as this did before, depended on the refusal for termination
// and deleted a locked entry that followed an unlocked one (CR-8, SI-076).
int IecCommandChannel::do_scratch(filename_t filenames[], int n)
{
    REFUSE_WHEN_WRITE_PROTECTED();
    DBGIECV("Scratch %d files:\n", n);
    mstring work;
    int scratched = 0;
    bool protected_medium = false;
    for(int i=0;i<n;i++) {
        GETPARTITION(filenames[i].partition, partition, 0);
        if (resolve_directory_path(fm, partition, filenames[i].path, work) != FR_OK) {
            drive->set_error(ERR_DIRECTORY_ERROR, partition->GetPartitionNumber(), 0);
            return 0;
        }
        scratched += scratch_matching(fm, work.c_str(), filenames[i].filename.c_str(),
                                      &protected_medium);
    }
    if (protected_medium) {
        return ERR_WRITE_PROTECT_ON; // a disk locked by EL:$ (SI-077a)
    }
    // Scratching nothing is not an error: the answer is 01 with a count of zero (SI-033).
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
    // The documented BASIC form adds 96 to the secondary address, as in
    // PRINT#15,"P"CHR$(96+2)..., while other programs send it bare. CBM DOS masks the
    // byte to its low four bits only from 19 up, so both forms reach the same channel
    // and a byte just outside the channel range is still refused.
    if (chan >= 19) {
        chan &= 0x0F;
    }
    // A byte that names no data channel, or a channel with nothing open on it, is
    // answered as the ROM at $E207 and SD parse_position() answer it (SI-081).
    if ((chan < 0) || (chan > 14)) {
        return ERR_NO_CHANNEL;
    }
    IecChannel *channel = drive->get_data_channel(chan);
    if (channel->state == e_buffer) {
        return do_buffer_position(chan, pos);
    }
    if (!channel->f) {
        return ERR_NO_CHANNEL;
    }
    FRESULT fres;
    switch(channel->state) {
    case e_complete: // a file read to its end is still open (SI-082)
    case e_file:
        if ((channel->name_to_open.access == e_write) || (channel->name_to_open.access == e_append)) {
            // A channel that writes (SI-083): what it holds goes out at the old position,
            // then the file moves, past its end if the medium can grow a file that way, and
            // the next byte lands there. Nothing is read back, because the file is not open
            // for reading. A medium that cannot put the file there is full, 72, which CBM
            // DOS names, rather than 69 (SI-036).
            if (channel->pointer > 0) {
                uint32_t written;
                fres = channel->f->write(channel->buffer, channel->pointer, &written);
                if (fres != FR_OK) {
                    drive->set_error_fres(fres);
                    return 0;
                }
                channel->pointer = 0;
            }
            fres = channel->f->seek(pos);
            if ((fres != FR_OK) || (channel->f->get_size() < pos)) {
                if (pos > channel->f->get_size()) {
                    drive->set_error(ERR_DISK_FULL, 0, 0);
                } else {
                    drive->set_error_fres(fres);
                }
                return 0;
            }
            drive->set_error(ERR_ALL_OK, 0, 0);
            state = e_idle;
            return 0;
        }
        fres = channel->f->seek(pos + channel->dataOffset); // past an x00 header
        if (fres != FR_OK) {
            drive->set_error_fres(fres);
            return 0;
        }
        channel->state = e_file;
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
    // -1 is the current partition. 0 is the system partition, which this drive does not
    // have, so it reads back as a partition that is not there, numbered 0 (SI-041).
    IecPartition *p = (part == 0) ? NULL : drive->vfs->GetPartition(part);
    memset(buffer, 0, 30);
    buffer[30] = 0x0d;
    drive->set_error(0, 0, 0);

    // Byte 0 is the type, byte 1 is reserved and byte 2 is the partition number. A
    // partition that does not exist is not an error: type 0 is what CMD DOS calls
    // "not created", and the caller asked for exactly that answer.
    buffer[2] = (part == 0) ? 0 : (uint8_t)drive->vfs->GetTargetPartitionNumber(part);

    if (p) {
        buffer[0] = (uint8_t)iec_partition_type(fm, p);

        // The name in bytes 3 to 18 is the one the partition directory shows, which
        // is the partition's own name rather than the path it is rooted at.
        char cbm_name[24];
        FileInfo info(40);
        filetype_t ftype = e_any;
        strncpy(info.lfname, p->GetName(), info.lfsize);
        info.lfname[info.lfsize - 1] = 0;
        IecPartition::CreateIecName(&info, cbm_name, ftype);

        // Padded with shifted spaces, as a CMD partition directory and SD
        // parse_getpartition() pad it (SI-041).
        memset(buffer + 3, 0xA0, 16);
        memcpy(buffer + 3, cbm_name, (strlen(cbm_name) < 16) ? strlen(cbm_name) : 16);

        // Bytes 27 to 29, the size in 512 byte blocks, as the HD counts them, clamped to
        // 24 bits: the image file for a partition rooted in a disk image, the volume for
        // one rooted in a directory (SI-041, SD parse_getpartition()).
        uint32_t blocks = 0;
        FileInfo root(8);
        if (fm->is_path_valid(p->GetRootPath(), &root) && root.fs && root.fs->supports_direct_sector_access()) {
            mstring image(p->GetRootPath());
            const char *ip = image.c_str();
            if (image.length() && (ip[image.length() - 1] == '/')) {
                mstring trimmed(ip, 0, image.length() - 2);
                image = trimmed;
            }
            FileInfo file(8);
            if (fm->fstat(image.c_str(), file) == FR_OK) {
                blocks = (file.size + 511) / 512;
            }
        } else {
            Path root_path(p->GetRootPath());
            uint32_t clusters = 0, cluster_size = 0;
            if (fm->get_total(&root_path, clusters, cluster_size) == FR_OK) {
                uint64_t total = ((uint64_t)clusters * cluster_size) / 512;
                blocks = (total > 0xFFFFFF) ? 0xFFFFFF : (uint32_t)total;
            }
        }
        buffer[27] = (uint8_t)(blocks >> 16);
        buffer[28] = (uint8_t)(blocks >> 8);
        buffer[29] = (uint8_t)blocks;
    }

    // Thirty bytes of information plus the carriage return that ends the reply.
    last_byte = 30;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 31;
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

// L[n][path]:name toggles the lock of the first file or directory whose CBM name matches
// (SI-076, HD 9-30). The entry is found through the directory, as a scratch finds it, so
// the name matches the entry a listing shows.
// The attributes an entry can carry on the media this drive serves (SI-076, SI-077).
static uint8_t fat_attributes_of(uint8_t iec_bits)
{
    uint8_t attrib = 0;
    if (iec_bits & IEC_ATTR_LOCKED) {
        attrib |= AM_RDO;
    }
    if (iec_bits & IEC_ATTR_HIDDEN) {
        attrib |= AM_HID;
    }
    if (iec_bits & IEC_ATTR_ARCHIVE) {
        attrib |= AM_ARC;
    }
    return attrib;
}

// L and EH: one entry, one attribute, turned over (SI-076, SI-077).
int IecCommandChannel::do_toggle_attributes(filename_t& name, uint8_t bits)
{
    REFUSE_WHEN_WRITE_PROTECTED();
    GETPARTITION(name.partition, partition, -1);
    mstring work;
    FileInfo info(INFO_SIZE);
    uint8_t mask = fat_attributes_of(bits);
    FRESULT fres = resolve_existing_iec_path(fm, partition, name, e_any, true, true, true, work, &info);
    if (fres == FR_OK) {
        fres = fm->set_attributes(work.c_str(), info.attrib ^ mask, mask);
    }
    if (fres == FR_NOT_ENABLED) {
        return ERR_SYNTAX_ERROR_GEN; // the medium does not carry this attribute
    }
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
    }
    return 0;
}

// Sets the attributes in `mask` to those in `attrib` on every entry a name matches, as
// the directory shows it. Directories are included, because L already locks one
// (SI-076) and a lock that EL and L disagreed about would be two locks.
static int set_attributes_matching(FileManager *fm, const char *dir_path, const char *pattern,
                                   uint8_t attrib, uint8_t mask, FRESULT *last_error)
{
    Directory *dir = NULL;
    if (fm->open_directory(dir_path, &dir) != FR_OK) {
        *last_error = FR_NO_PATH;
        return 0;
    }
    IndexedList<mstring *> matches(8, NULL);
    FileInfo info(INFO_SIZE);
    while (dir->get_entry(info) == FR_OK) {
        if ((info.attrib & AM_VOL) || !info.lfname[0]) {
            continue;
        }
        char cbm_name[24];
        filetype_t ftype = e_any;
        iec_entry_name(fm, dir_path, &info, cbm_name, ftype);
        if (!pattern_match(pattern, cbm_name, false)) {
            continue;
        }
        // The entries are collected before any of them is changed, so the directory is
        // not written while it is being read.
        char entry[80];
        mstring *full = new mstring(dir_path);
        append_path_component(*full, info.generate_fat_name(entry, sizeof(entry)));
        matches.append(full);
    }
    delete dir;

    int changed = 0;
    for (int i = 0; i < matches.get_elements(); i++) {
        FRESULT fres = fm->set_attributes(matches[i]->c_str(), attrib, mask);
        if (fres == FR_OK) {
            changed++;
        } else {
            *last_error = fres;
        }
        delete matches[i];
    }
    return changed;
}

// EL, EU and A (SI-077).
int IecCommandChannel::do_set_attributes(filename_t names[], int n, uint8_t attrib, uint8_t mask)
{
    REFUSE_WHEN_WRITE_PROTECTED();
    mstring work;
    FRESULT last_error = FR_OK;
    int changed = 0;
    for (int i = 0; i < n; i++) {
        GETPARTITION(names[i].partition, partition, 0);
        if (resolve_directory_path(fm, partition, names[i].path, work) != FR_OK) {
            drive->set_error(ERR_DIRECTORY_ERROR, partition->GetPartitionNumber(), 0);
            return 0;
        }
        if ((mask == IEC_ATTR_LOCKED) && !strcmp(names[i].filename.c_str(), "$")) {
            // EL:$ and EU:$ lock the disk image the directory is in (SI-077a).
            FRESULT fres = fm->set_write_lock(work.c_str(), attrib != 0);
            if (fres == FR_NOT_ENABLED) {
                return ERR_SYNTAX_ERROR_GEN; // a host directory records no such lock
            }
            if (fres != FR_OK) {
                drive->set_error_fres(fres);
                return 0;
            }
            changed++;
            continue;
        }
        changed += set_attributes_matching(fm, work.c_str(), names[i].filename.c_str(),
                                           fat_attributes_of(attrib), fat_attributes_of(mask),
                                           &last_error);
    }
    if (!changed) {
        if (last_error == FR_NOT_ENABLED) {
            return ERR_SYNTAX_ERROR_GEN; // the medium does not carry these attributes
        }
        if (last_error == FR_WRITE_PROTECTED) {
            return ERR_WRITE_PROTECT_ON;
        }
        return ERR_FILE_NOT_FOUND;
    }
    if (last_error != FR_OK) {
        drive->set_error_fres(last_error);
    }
    return 0;
}

// R-H (SI-064). A directory's header is what a listing of it shows (SI-065): the disk
// name inside a CBM image, which the file system holds; the directory's own name on a
// host file system, where the name in the parent is all there is; and the partition's
// name at the root of a partition.
int IecCommandChannel::do_set_header(filename_t& name, const char *id)
{
    REFUSE_WHEN_WRITE_PROTECTED();
    GETPARTITION(name.partition, partition, 0);
    mstring work, relative;
    FRESULT fres = resolve_directory_path(fm, partition, name.path, work, &relative);
    if (fres != FR_OK) {
        drive->set_error(ERR_DIRECTORY_ERROR, drive->vfs->GetTargetPartitionNumber(name.partition), 0);
        return 0;
    }
    fres = fm->set_dir_label(work.c_str(), name.filename.c_str(), id);
    if (fres != FR_NOT_ENABLED) {
        if (fres != FR_OK) {
            drive->set_error_fres(fres);
        }
        return 0;
    }

    // No label of its own: the name the parent holds is the header.
    const char *rel = relative.c_str();
    int rel_len = strlen(rel);
    while ((rel_len > 0) && (rel[rel_len - 1] == '/')) {
        rel_len--;
    }
    if (rel_len == 0) {
        // The partition's name, which keeps its first sixteen characters as R-P does
        // (SI-051).
        char cut_name[17];
        strncpy(cut_name, name.filename.c_str(), 16);
        cut_name[16] = 0;
        partition->SetName(cut_name);
        return 0;
    }
    char fat_name[52];
    petscii_to_fat(name.filename.c_str(), fat_name, sizeof(fat_name));
    mstring renamed;
    int cut = strlen(work.c_str());
    while ((cut > 0) && (work[cut - 1] == '/')) {
        cut--;
    }
    while ((cut > 0) && (work[cut - 1] != '/')) {
        cut--;
    }
    renamed.copy(work.c_str(), 0, cut - 1);
    append_path_component(renamed, fat_name);
    fres = fm->rename(work.c_str(), renamed.c_str());
    if (fres != FR_OK) {
        drive->set_error_fres(fres);
        return 0;
    }

    // The drive may be standing in the directory it renamed, or below it, so its current
    // directory follows the new name.
    int start = rel_len;
    while ((start > 0) && (rel[start - 1] != '/')) {
        start--;
    }
    mstring current(partition->GetRelativePath());
    if (!strncasecmp(current.c_str(), rel, rel_len) &&
        ((current[rel_len] == 0) || (current[rel_len] == '/'))) {
        mstring moved;
        moved.copy(rel, 0, start - 1);
        moved += fat_name;
        moved += current.c_str() + rel_len;
        if (!partition->cd(moved.c_str())) {
            partition->cd("/");
        }
    }
    return 0;
}

// R-P (SI-051). The old name is the name a partition carries, not a path, so the
// partition list is searched for it. A partition name is sixteen characters, as on
// the CMD devices, so every place that shows it shows the same name.
int IecCommandChannel::do_rename_partition(const char *newname, const char *oldname)
{
    REFUSE_WHEN_WRITE_PROTECTED();
    char name[17];
    strncpy(name, newname, 16);
    name[16] = 0;
    for (int i = 1; i < MAX_PARTITIONS; i++) {
        IecPartition *p = drive->vfs->GetPartition(i);
        if (p && (p->GetPartitionNumber() == i) && !strcasecmp(p->GetName(), oldname)) {
            p->SetName(name);
            return 0;
        }
    }
    return ERR_PARTITION_ERROR;
}

// U0> (SI-100). The drive answers this command on the number it was addressed on and
// every later one on the new number.
int IecCommandChannel::do_set_device_number(int dev)
{
    drive->set_device_number(dev);
    return 0;
}

int64_t IecCommandChannel::get_clock_offset(void)
{
    return drive->get_clock_offset();
}

void IecCommandChannel::set_clock_offset(int64_t seconds)
{
    drive->set_clock_offset(seconds);
}

int IecCommandChannel::do_set_write_protect(bool on)
{
    drive->set_write_protect(on);
    return 0;
}

int IecCommandChannel::do_restore_device_number()
{
    drive->set_device_number(drive->configured_device_number());
    return 0;
}

int IecCommandChannel::ext_open_file(const char *filenameOrCommand)
{
    // A whole buffer's worth, so that a command too long for it reaches the length check
    // and is refused rather than executed cut short (SI-022).
    strncpy((char *) wr_buffer, filenameOrCommand, CBMDOS_COMMAND_BUFFER_SIZE);
    wr_buffer[CBMDOS_COMMAND_BUFFER_SIZE] = 0;
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
            state = e_idle; // a reply nobody read belongs to the command before this one
            err = parser->execute_command(wr_buffer, wr_pointer);
            if (err) set_error(err);
            if (drive_failed()) {
                log_line("command failed", wr_buffer, wr_pointer, NULL, NULL, 0);
            } else if (drive->log_every_operation()) {
                // A command that answers with data, such as M-R or G-P, has it in the buffer.
                bool reply = (state == e_status) && (last_byte >= 0) && (last_byte < 256);
                log_line("command", wr_buffer, wr_pointer, reply ? "reply" : NULL, buffer, reply ? (last_byte + 1) : 0);
            }
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
    IecDriveLock guard(drive);
    File *fi;
    FRESULT fres = fm->fopen(path, file, FA_READ, &fi);
    if (fres == FR_OK) {
        LoadPartitions(fi);
        fm->fclose(fi);
    }
}

FRESULT IecFileSystem :: SavePartitions(const char *path, const char *filename)
{
    IecDriveLock guard(drive);
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

// A partition list replaces the one the drive has: a partition the file does not name is
// removed (#934). A file with no usable entry leaves the list alone, so the drive is never
// left without a partition.
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
            bool named[MAX_PARTITIONS] = { false };
            int loaded = 0;
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
                named[part] = true;
                loaded++;
            }
            for (int i = 1; (i < MAX_PARTITIONS) && loaded; i++) {
                if (partitions[i] && !named[i]) {
                    RemovePartition(i);
                }
            }
            if (loaded && !partitions[currentPartition]) {
                for (int i = 1; i < MAX_PARTITIONS; i++) {
                    if (partitions[i]) {
                        currentPartition = i;
                        break;
                    }
                }
            }
        }
    }
    delete root_json;
    delete[] buffer;
}
