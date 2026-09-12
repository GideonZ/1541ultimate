#include "iec_channel.h"
#include "dump_hex.h"
#include "rtc.h"
#include "iec_trace.h"
#include <stdarg.h>

/* ------------------------------------------------------------------------------
 * Software IEC compatibility diagnostics for GideonZ/1541ultimate#877.
 *
 * One line per completed high level operation, never one per bus byte. Every line
 * starts with SOFTIEC_TRACE_PREFIX, so `grep SOFTIEC-TRACE` on a device log finds
 * all of them and a search of this file finds every call site. Removing the feature
 * means deleting this block, software/io/iec/iec_trace.h and the SOFTIEC_TRACE()
 * calls below; -DSOFTIEC_TRACE_ENABLED=0 turns it off instead. No command behaviour
 * depends on it.
 * ------------------------------------------------------------------------------ */

#if SOFTIEC_TRACE_ENABLED

// The sequence number and the line buffers are shared. Two tasks can reach the drive:
// the IEC task, and the task behind the menu and the REST interface. Two lines written
// at the same instant can therefore interleave or share a number. That is a defect a
// reader can see rather than one that misleads, and keeping the buffers out of the IEC
// task's small stack matters more.
static uint32_t softiec_trace_sequence = 0;

static const char *softiec_trace_stream(stream_type_t s)
{
    switch (s) {
    case e_stream_file:       return "file";
    case e_stream_buffer:     return "buffer";
    case e_stream_dir:        return "dir";
    case e_stream_partitions: return "partitions";
    default:                  return "?";
    }
}

static const char *softiec_trace_filetype(filetype_t t)
{
    switch (t) {
    case e_any:    return "any";
    case e_prg:    return "prg";
    case e_seq:    return "seq";
    case e_usr:    return "usr";
    case e_rel:    return "rel";
    case e_folder: return "dir";
    default:       return "?";
    }
}

static const char *softiec_trace_access(fileaccess_t a)
{
    switch (a) {
    case e_not_set: return "none";
    case e_read:    return "read";
    case e_write:   return "write";
    case e_append:  return "append";
    default:        return "?";
    }
}

// Writes one diagnostic line. The payload is passed with its length and is never
// treated as a string, so embedded zeroes, carriage returns and shifted PETSCII
// bytes all reach the log. The buffers are static because the IEC task has a small
// stack; a line is built and printed in one go.
void softiec_trace(IecDrive *drive, int channel, uint8_t secondary,
                   const char *op, const uint8_t *payload, int len,
                   const char *detail_fmt, ...)
{
    static char hex[SOFTIEC_TRACE_HEX_SIZE];
    static char txt[SOFTIEC_TRACE_TEXT_SIZE];
    static char err[80];
    static char detail[320];

    softiec_trace_hex(payload, len, hex, sizeof(hex));
    softiec_trace_text(payload, len, txt, sizeof(txt));

    detail[0] = 0;
    if (detail_fmt) {
        va_list ap;
        va_start(ap, detail_fmt);
        vsnprintf(detail, sizeof(detail), detail_fmt, ap);
        va_end(ap);
    }

    // The drive's own rendering of the error channel. Reading it here does not clear
    // it; only the command channel's reader does that. The carriage return it ends
    // in would split the log line, so it goes.
    int n = drive->get_error_string(err);
    if ((n < 0) || (n >= (int)sizeof(err))) {
        n = 0;
    }
    while ((n > 0) && ((err[n - 1] == 0x0D) || (err[n - 1] == 0x0A))) {
        n--;
    }
    err[n] = 0;

    // An operation that carries no payload leaves the payload fields out rather than
    // printing three empty ones, which keeps the common lines short.
    if (payload && (len > 0)) {
        printf(SOFTIEC_TRACE_PREFIX " #%d %s dev=%d sa=$%02X chan=%d len=%d hex=[%s] txt=\"%s\" %s err=%s\n",
               (int)(++softiec_trace_sequence), op, (int)drive->get_address(), (int)secondary,
               channel, len, hex, txt, detail, err);
    } else {
        printf(SOFTIEC_TRACE_PREFIX " #%d %s dev=%d sa=$%02X chan=%d %s err=%s\n",
               (int)(++softiec_trace_sequence), op, (int)drive->get_address(), (int)secondary,
               channel, detail, err);
    }
}

#endif /* SOFTIEC_TRACE_ENABLED */

// The bytes that crossed a channel are counted, not logged. Only the first sixteen
// and the last sixteen are kept, which is enough to see that the right file went by,
// and a block costs a constant amount of work rather than a cost per byte.
//
// The count is of bytes the host took off the bus, taken where the channel is told
// they were accepted. Counting where the channel prefetches instead would count a
// byte the host was offered and did not take, and a buffer position command rewinds
// the same pointer as a retry does, so the two cannot be told apart there.
void IecChannel::trace_reset_counters(void)
{
    trace_rd = 0;
    trace_wr = 0;
    trace_dropped = 0;
    trace_faulted = false;
}

// One line the first time a channel fails, so a channel that fails on every byte
// does not fill the log.
void IecChannel::trace_fault(const char *what, int rv)
{
    if (trace_faulted) {
        return;
    }
    trace_faulted = true;
    SOFTIEC_TRACE(drive, channel, (uint8_t)(0x60 | channel), "FAULT", NULL, 0,
                  "at=%s retval=%d state=%d", what, rv, (int)state);
}


void IecChannel::trace_record_read(const uint8_t *data, int len)
{
#if SOFTIEC_TRACE_ENABLED
    if (!data || (len <= 0)) {
        return;
    }
    for (int i = 0; (i < len) && (trace_rd + i < 16); i++) {
        trace_rd_head[trace_rd + i] = data[i];
    }
    int keep = (len > 16) ? 16 : len;
    for (int i = 0; i < keep; i++) {
        trace_rd_ring[(trace_rd + len - keep + i) & 15] = data[len - keep + i];
    }
    trace_rd += len;
#endif
}

// The bytes at the read pointer that the host has just taken.
void IecChannel::trace_record_pop(int n)
{
#if SOFTIEC_TRACE_ENABLED
    if ((n <= 0) || (pointer < 0) || (pointer >= 512)) {
        return;
    }
    if ((pointer + n) > 512) {
        n = 512 - pointer;
    }
    trace_record_read(&buffer[pointer], n);
#endif
}

void IecChannel::trace_record_write(uint8_t b)
{
#if SOFTIEC_TRACE_ENABLED
    if (trace_wr < 16) {
        trace_wr_head[trace_wr] = b;
    }
    trace_wr_ring[trace_wr & 15] = b;
    trace_wr++;
#endif
}

#if SOFTIEC_TRACE_ENABLED

// Copies the last bytes of a ring back into the order they arrived in.
static int softiec_trace_tail(const uint8_t *ring, uint32_t count, uint8_t *out)
{
    int n = (count > 16) ? 16 : (int)count;
    for (int i = 0; i < n; i++) {
        out[i] = ring[(count - n + i) & 15];
    }
    return n;
}

// Renders a byte count and the window that was kept, as "0", or "13 [41 42 ...]", or
// "1264 [ ...sixteen... ... ...sixteen... ]". The caller's buffer needs 128 characters.
// The two scratch buffers are static to keep them off the IEC task's stack; the result
// is copied into the caller's buffer before this returns, so two calls in one argument
// list do not tread on each other.
static const char *softiec_trace_flow(uint32_t count, const uint8_t *head,
                                      const uint8_t *ring, char *out, int out_size)
{
    static char head_hex[3 * 16 + 4];
    static char tail_hex[3 * 16 + 4];
    static uint8_t tail[16];
    int n = (count > 16) ? 16 : (int)count;
    softiec_trace_hex(head, n, head_hex, sizeof(head_hex));
    int m = softiec_trace_tail(ring, count, tail);
    softiec_trace_hex(tail, m, tail_hex, sizeof(tail_hex));
    if (count == 0) {
        snprintf(out, out_size, "0");
    } else if (count <= 16) {
        snprintf(out, out_size, "%d [%s]", (int)count, head_hex);
    } else {
        snprintf(out, out_size, "%d [%s ... %s]", (int)count, head_hex, tail_hex);
    }
    return out;
}

#endif /* SOFTIEC_TRACE_ENABLED */

IecChannel::IecChannel(IecDrive *dr, int ch)
{
    trace_reset_counters();
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
    trace_reset_counters();
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
        trace_record_pop(pop_size);
        pointer += pop_size;
        if (pointer == 512) {
            if (read_block()) { // also resets pointer.
                trace_fault("read_block while streaming", IEC_READ_ERROR);
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
        }
        trace_record_pop(pop_size);
        pointer += pop_size;
        if (pointer == prefetch_max) {
            while (read_dir_entry() > 0)
                ;
            return IEC_OK;
        }
        break;
    case e_record:
        trace_record_pop(pop_size);
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
        trace_record_pop(pop_size);
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
            trace_record_pop(1);
            if (read_block()) { // also resets pointer.
                trace_fault("read_block while streaming", IEC_READ_ERROR);
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
            trace_record_pop(1);
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

    trace_record_pop(1);
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
        trace_fault("read_block", IEC_READ_ERROR);
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
        if (pointer < 64) {
            buffer[pointer++] = b;
        } else {
            trace_dropped++; // #877: the name is longer than the channel buffer
        }
        break;

    case e_record:
        if (pointer == recordSize) {
            // issue error 50
            drive->get_command_channel()->set_error(ERR_OVERFLOW_IN_RECORD, 0, 0);
            trace_fault("push_data record overflow", IEC_BYTE_LOST);
            return IEC_BYTE_LOST;
        } else {
            buffer[pointer++] = b;
            trace_record_write(b);
            recordDirty = true;
        }
        break;
        // the actual writing does not happen here, because it is initiated by EOI

    case e_file:
        buffer[pointer++] = b;
        trace_record_write(b);
        if (pointer == 512) {
            FRESULT res = FR_DENIED;
            if (f) {
                res = f->write(buffer, 512, &bytes);
            }
            if (res != FR_OK) {
                fm->fclose(f);
                f = NULL;
                state = e_error;
                trace_fault("push_data block write", IEC_WRITE_ERROR);
                return IEC_WRITE_ERROR;
            }
            pointer = 0;
        }
        break;

    case e_buffer:
        if (pointer < 256) {
            buffer[pointer++] = b;
            trace_record_write(b);
        }
        break;

    default:
        trace_fault("push_data on a channel that is not writing", IEC_BYTE_LOST);
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
        {
            // One exit, so the #877 diagnostics see every outcome of a close.
            t_channel_retval rv = IEC_OK;
            if ((name_to_open.access == e_write) || (name_to_open.access == e_append)) {
                if (f) {
                    if (pointer > 0) {
                        uint32_t dummy;
                        FRESULT res = f->write(buffer, pointer, &dummy);
                        if (res != FR_OK) {
                            state = e_error;
                            rv = IEC_WRITE_ERROR;
                        }
                    }
                    if (rv == IEC_OK) {
                        close_file();
                        state = e_idle;
                    }
                } else {
                    state = e_error;
                    rv = IEC_WRITE_ERROR;
                }
            } else if (state == e_record) {
                state = e_idle;
                rv = write_record();
                close_file();
            } else {
                close_file();
                state = e_idle;
            }
            static char rd[128], wr[128];
            SOFTIEC_TRACE(drive, channel, (uint8_t)(0xE0 | channel), "CLOSE", NULL, 0,
                          "read=%s written=%s result=%d",
                          softiec_trace_flow(trace_rd, trace_rd_head, trace_rd_ring, rd, sizeof(rd)),
                          softiec_trace_flow(trace_wr, trace_wr_head, trace_wr_ring, wr, sizeof(wr)),
                          (int)rv);
            return rv;
        }
    case 0x60:
        SOFTIEC_TRACE(drive, channel, (uint8_t)(0x60 | channel), "ADDR", NULL, 0,
                      "addressed for data, state=%d", (int)state);
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

int IecChannel::read_dir_entry(void)
{
    FileInfo info(40);
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
    memcpy(&buffer[27 - chars], partition_type ? partition_type : types[(int)ftype], 3);

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

    // Existing REL files also open by name alone (for example the E.DATA editor).
    // Resolve the type before choosing access flags and setting up record I/O.
    if (name_to_open.filetype == e_any) {
        FileInfo info(48);
        FRESULT fres = fm->fstat(full_path, info);
        if (fres == FR_NO_FILE) {
            GETPARTITION(name_to_open.file.partition, partition, 0);
            fres = resolve_existing_iec_path(fm, partition, name_to_open.file, e_any,
                                             false, true, true, work, &info);
            full_path = work.c_str();
        }
        if (fres != FR_OK) {
            drive->set_error_fres(fres);
            return 0;
        }
        char cbm_name[24];
        IecPartition::CreateIecName(&info, cbm_name, name_to_open.filetype);
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
                recordOffset = 2; // First record follows the REL header, also on a reused channel.
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
#if SOFTIEC_TRACE_ENABLED
    // #877 diagnostics: the name as the bus delivered it, taken now because setting
    // up the stream reads the first block of the file into the same buffer. The
    // rendering is cut at SOFTIEC_TRACE_MAX_BYTES; the reported length is the real one.
    static uint8_t trace_raw_name[256];
    int raw_kept = (pointer > (int)sizeof(trace_raw_name)) ? (int)sizeof(trace_raw_name) : pointer;
    if (raw_kept > 0) {
        memcpy(trace_raw_name, buffer, raw_kept);
    }
#endif
    trace_reset_counters();
    buffer[pointer] = 0; // string terminator
    DBGIECV("Open file. Raw Filename = '%s'\n", buffer);
    int parse_err = parse_open((const char *)buffer, name_to_open);
    if (parse_err) {
        state = e_error;
        drive->set_error(parse_err, 0, 0);
        SOFTIEC_TRACE(drive, channel, (uint8_t)(0xF0 | channel), "OPEN", trace_raw_name, raw_kept,
                      "parse=%d", parse_err);
        return -1;
    }

    IecPartition *partition = drive->vfs->GetPartition(name_to_open.file.partition);
    recordSize = 0;

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
    // Where the open landed matters as much as what was asked for, so the line
    // carries the partition's working directory and, for a file, the host path and
    // the size the drive found there.
    if (f) {
        SOFTIEC_TRACE(drive, channel, (uint8_t)(0xF0 | channel), "OPEN", trace_raw_name, raw_kept,
                      "parse=0 dropped=%d stream=%s type=%s access=%s part=%d cwd=%s host=%s size=%d result=%d",
                      (int)trace_dropped,
                      softiec_trace_stream(name_to_open.dir_opt.stream),
                      softiec_trace_filetype(name_to_open.filetype),
                      softiec_trace_access(name_to_open.access),
                      name_to_open.file.partition,
                      partition ? partition->GetFullPath() : "-",
                      f->get_path(), (int)f->get_size(), result);
    } else {
        SOFTIEC_TRACE(drive, channel, (uint8_t)(0xF0 | channel), "OPEN", trace_raw_name, raw_kept,
                      "parse=0 dropped=%d stream=%s type=%s access=%s part=%d cwd=%s result=%d",
                      (int)trace_dropped,
                      softiec_trace_stream(name_to_open.dir_opt.stream),
                      softiec_trace_filetype(name_to_open.filetype),
                      softiec_trace_access(name_to_open.access),
                      name_to_open.file.partition,
                      partition ? partition->GetFullPath() : "-", result);
    }
    return result;
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
    trace_secondary = (uint8_t)(0x60 | 15);
    trace_cmd_dropped = 0;
}

IecCommandChannel::~IecCommandChannel()
{
    delete parser;
}

void IecCommandChannel::reset(void)
{
    IecChannel::reset();
    set_error(ERR_DOS);
    SOFTIEC_TRACE(drive, 15, (uint8_t)(0x60 | 15), "RESET", NULL, 0, "drive reset");
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
        // Logged before the string is fetched, because fetching it clears the error.
        // The err= field is therefore the answer the host is about to read.
        SOFTIEC_TRACE(drive, 15, (uint8_t)(0x60 | 15), "STATUS", NULL, 0, "read by host");
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
    trace_cmd_dropped++; // #877: the command is longer than the command buffer
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
    } else if (fres == FR_INVALID_PARAMETER) { // outside the disk (SI-036)
        drive->set_error(ERR_ILLEGAL_TRACK_SECTOR, track, sector);
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
    } else if (fres == FR_INVALID_PARAMETER) { // outside the disk (SI-036)
        drive->set_error(ERR_ILLEGAL_TRACK_SECTOR, track, sector);
    } else {
        drive->set_error_fres(fres);
    }
    state = e_idle;
    return 0;
}

int IecCommandChannel::do_block_allocate(int part, int track, int sector, bool alloc)
{
    if (part >= MAX_PARTITIONS) {
        set_error(ERR_SYNTAX_ERROR_CMD);
        return ERR_SYNTAX_ERROR_CMD;
    }
    GETPARTITION(part, partition, -1);
    Path path(partition->GetFullPath());
    FRESULT fres;
    fres = fm->fs_allocate_sector(&path, track, sector, alloc);
    if (fres == FR_DENIED) {
        drive->set_error(ERR_DENIED, track, sector);
    } else if (fres == FR_INVALID_PARAMETER) { // outside the disk (SI-036)
        drive->set_error(ERR_ILLEGAL_TRACK_SECTOR, track, sector);
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
    IecChannel *channel;
    channel = drive->get_data_channel(chan);
    channel->pointer = pos & 0xFF; // CBM DOS keeps only the low byte, so it wraps
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

    // The left arrow, PETSCII 0x5F, means the parent directory both after the command
    // word, as in "CD_", and behind a colon, as in "CD:_" or "CD/SUB/:_". Only the
    // first arrives as a path, so move the second there and both take one route.
    if (dest.filename == "_") {
        dest.filename = "";
        if (dest.path.length() && (dest.path[-1] != '/')) {
            dest.path += "/";
        }
        dest.path += "_";
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
    SOFTIEC_TRACE(drive, 15, (uint8_t)(0x60 | 15), "REPLY", buffer, len, "command response");
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
    SOFTIEC_TRACE(drive, 15, (uint8_t)(0x60 | 15), "REPLY", buffer, len, "working directory");
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
    memset(buffer, 0, 30);
    buffer[30] = 0x0d;
    drive->set_error(0, 0, 0);

    // Byte 0 is the type, byte 1 is reserved and byte 2 is the partition number. A
    // partition that does not exist is not an error: type 0 is what CMD DOS calls
    // "not created", and the caller asked for exactly that answer.
    buffer[2] = (uint8_t)drive->vfs->GetTargetPartitionNumber(part);

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

        strncpy((char *)(buffer+3), cbm_name, 16);
        buffer[27] = 0xFF; // for now always reporting 0xFF0000 as partition size
    }

    // Thirty bytes of information plus the carriage return that ends the reply.
    last_byte = 30;
    pointer = 0;
    prefetch = 0;
    prefetch_max = 31;
    state = e_status;
    SOFTIEC_TRACE(drive, 15, (uint8_t)(0x60 | 15), "REPLY", buffer, 31, "partition info");
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
        trace_secondary = (uint8_t)(0x60 | 15);
        reset_prefetch();
        break;
    case 0xE0:
    case 0xF0:
        trace_secondary = (uint8_t)(b | 15);
        pointer = 0;
        wr_pointer = 0;
        trace_cmd_dropped = 0;
        SOFTIEC_TRACE(drive, 15, trace_secondary, (b == 0xF0) ? "OPEN" : "CLOSE",
                      NULL, 0, "channel=command");
        break;
    case 0x00: // end of data, command received in buffer
        wr_buffer[wr_pointer] = 0;
        if (wr_pointer) {
            drive->set_error(0, 0, 0);
            err = parser->execute_command(wr_buffer, wr_pointer);
            if (err) set_error(err);
            SOFTIEC_TRACE(drive, 15, trace_secondary, "CMD", wr_buffer, wr_pointer,
                          "dropped=%d result=%d", (int)trace_cmd_dropped, err);
        }
        wr_pointer = 0;
        trace_cmd_dropped = 0;
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
