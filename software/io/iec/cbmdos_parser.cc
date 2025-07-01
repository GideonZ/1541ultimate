// Written by Gideon (2025)
// This module handles filename and command parsing for access to the VFS
// from the side of the commodore in a CMDHD / SD2IEC fashion

// Examples:
// LOAD"[[n][path]:]pattern",dv[,sa]
// SAVE"[[@][n][path]:]filename",dv
// VERIFY"[[n][path]:]pattern",dv[,sa] -> drive doesn't know what verify is; it's just open for reading
//
// Filename pattern: [[n][path]:]pattern,X,Y
// Find colon; if none: just pattern.
// If found, pattern = after colon. What is before colon:
// If it starts with @, set the replace flag and shift to next
// parse partition number until / or end of string, fail if non-numeric
// set the remaining string as path, which is possibly empty. Paths are relative to the current directory of the partition.
// after the colon, look for comma. If comma is found, read file type and access indicators

// What is missing in the above is the file type clue and the file access mode. This is thus only applicable to the file open sequence,
// and does NOT apply to commands. For commands the ,(p|u|s|l) and (,r,w,a) does not apply.

// FN = [[n][path]:]filename; if * or ? in filename then pattern flag is set
// Commands
// R[ENAME][[n][path]:]filename = [[n][path]:]pattern
// S[CRATCH] commalist([[n][path]:]pattern)
// C[OPY] [[n][path]:]filename = commalist([[n][path]:]pattern)
// P[OSITION] <binary value, up to 32-bit >
// MD [[n][path]:]filename
// CD [[n][path]:]filename, where <- in path means .., // means root
// RD [n]:subname; the parser could also just use 'FN' and then check if path is empty
// CP [n]
// C{shift}P <binary value>
// N [n]:subname[,id]

// RTC access:
// T-RA -> "dow. MM/DD/YY HH:mm:ss xm\r"
// T-RI -> "YYYY-MM-DDThh:mm:ss dow\r"
// T-RD -> "{WD}{Y}{M}{D}{h}{m}{s}{am/pm}\r"  Documentation says it's decimal, but it is actually binary h in 12 hour format, Y+1900
// T-RB -> "{WD}{Y}{M}{D}{h}{m}{s}{am/pm}\r"  BCD format Y<80:+2000, else+1900, also 12 hour format for some strange reason
// T-WA -> not supported, as the RTC is not part of the drive, but of the system
// T-WI -> not supported, as the RTC is not part of the drive, but of the system
// T-WD -> not supported, as the RTC is not part of the drive, but of the system
// T-WB -> not supported, as the RTC is not part of the drive, but of the system

// Directories
// $ [=T] [[n][path]:] [= commalist([TP|OPTION])], OPTION = { L, N, <stamp, >stamp }, stamp = MM/DD/YY HH:MM xM, x = {A | P}
// $ [=P] [:pattern] -> outputs a listing of current partitions

// Swaplists are not part of the drive, they are part of the user interface.

// Block commands should not be available outside of a CBM image, as you cannot expect a commodore program to know about
// the underlying file system. However, the B-A and B-F commands should be implemented within an image, and the buffer access
// methods should work.

#include "cbmdos_parser.h"

int parse_full_path(const char *buf, filename_t& name, bool *replace = NULL, bool path_only = false)
{
    //  [[@][n][path]:]pattern
    // $[=(T|P)][[n][path][:pattern[=commalist(type|option)]]]
    name.partition = -1;
    name.has_wildcard = false;
    name.path = "";
    name.filename = "";

    const char *colon = strchr(buf, ':');
    if (colon) {
        name.filename = colon + 1;
    }
    if (colon || path_only) {
        int idx = 0;
        if (buf[idx] == '@') {
            if (!replace) {
                return ERR_SYNTAX;
            }
            *replace = true;
            idx++;
        }
        if (isdigit(buf[idx])) {
            name.partition = 0;
            while(isdigit(buf[idx])) {
                name.partition *= 10;
                name.partition += (buf[idx++] - '0');
            }
        }
        if ((buf[idx] == '/') || (path_only && buf[idx] == '_')) {
            if (colon) {
                int colon_pos = colon - buf;
                name.path.copy(buf, idx, colon_pos-1);
            } else { // there is no colon, so we do path only
                name.path = buf + idx;
            }
        } else if(buf[idx] != ':') { // other non-zero char: error
            return ERR_SYNTAX;
        }
    } else { // no colon
        name.filename = buf;
    }
    name.has_wildcard = name.filename.contains_any("?*");
    if (!path_only && name.filename.length() == 0) {
        return ERR_ILLEGAL_NAME;
    }
    return 0;
}

int parse_dir_option(const char *buf, dir_options_t &opt)
{
    int M, d, y, h, h12, m, n;
    char ampm;
    uint32_t datetime;

    switch(buf[0]) {
    //case 'S': opt.timefmt = e_short; break;
    case 'L': opt.timefmt = e_long;  break;
    case 'P': opt.filetypes |= 0x01; break;
    case 'S': opt.filetypes |= 0x02; break;
    case 'U': opt.filetypes |= 0x04; break;
    case 'R': opt.filetypes |= 0x08; break;
    case 'B': opt.filetypes |= 0x10; break;
    case 'D': opt.filetypes |= 0x10; break;
    case 'H': opt.filetypes |= 0x20; break;
    case 'N': opt.timefmt = e_none; break;
    case '<':
    case '>':
        n = sscanf(buf+1, "%d/%d/%d %d:%d %c", &M, &d, &y, &h12, &m, &ampm);
        if (n != 6)
            return ERR_SYNTAX;
        // convert am/pm time back to normal time format
        y += (y < 80) ? 2000 : 1900;
        h = (h12 % 12) + (ampm == 'P' ? 12:0);
        datetime = make_fat_time(y, M, d, h, m, 0);
        if (buf[0] == '<')
            opt.max_datetime = datetime;
        else
            opt.min_datetime = datetime;

        break;
    default:
        return ERR_SYNTAX;    
    }
    return 0;
}

int parse_open(const char *buf, open_t& fn)
{
    fn.dir_opt = c_dir_options_init;
    fn.replace = false;
    fn.access = e_not_set;
    fn.filetype = e_any;

    int err = 0;
    if (buf[0] == '$') {
        fn.dir_opt.stream = e_dir_files;
        if (buf[1] == '=') {
            if (buf[2] == 'P') {
                fn.dir_opt.stream = e_dir_partitions;
            } else if(buf[2] == 'T') {
                fn.dir_opt.timefmt = e_short;
            }
            parse_full_path(buf+3, fn.file, &fn.replace, true);
        } else {
            parse_full_path(buf+1, fn.file, &fn.replace, true);
        }
    } else {
        err = parse_full_path(buf, fn.file, &fn.replace);
    }
    if (err) {
        return err;
    }

    if (fn.dir_opt.stream != e_file) {
        // Parse options / filter
        const char *rem;
        if (fn.file.filename.split('=', &rem)) {
            mstring opts(rem);
            const char *parts[6] = { NULL };
            int n = opts.split(',', parts, 6);
            for (int i=0;i<n;i++) {
                err = parse_dir_option(parts[i], fn.dir_opt);
                if (err) return err;
            }
        }
        return 0;
    }


    // check for file access modifiers
    while(fn.file.filename[-2] == ',') {
        switch (fn.file.filename[-1]) {
        case 'R': fn.access = e_read; break;
        case 'W': fn.access = e_write; break;
        case 'A': fn.access = e_append; break;
        case 'P': fn.filetype = e_prg; break;
        case 'S': fn.filetype = e_seq; break;
        case 'U': fn.filetype = e_usr; break;
        case 'L': fn.filetype = e_rel; break;
        default:
            return ERR_SYNTAX;
        }
        fn.file.filename.set(-2, '\0');
    }

    if (fn.file.filename.contains_any(",=:\xA0\r")) {
        return ERR_ILLEGAL_CHARS;
    }
    return 0;
}

int block_command(const uint8_t *buffer, int len)
{
    if (buffer[1] != '-') {
        return ERR_SYNTAX;
    }
    int n, chan, part, track, sector;
    switch(buffer[2]) {
    case 'R':
        n = sscanf((const char *)buffer+3, "%d%d%d%d", &chan, &part, &track, &sector);
        if (n != 4) return ERR_SYNTAX;
        return do_block_read(chan, part, track, sector);
    case 'W':
        n = sscanf((const char *)buffer+3, "%d%d%d%d", &chan, &part, &track, &sector);
        if (n != 4) return ERR_SYNTAX;
        return do_block_write(chan, part, track, sector);
    case 'P':
        n = sscanf((const char *)buffer+3, "%d%d", &chan, &part);
        if (n != 2) return ERR_SYNTAX;
        return do_buffer_position(chan, part);
    default:
        return ERR_UNKNOWN_CMD;
    }
    return 0;
}

int cp_command(const uint8_t *buffer, int len)
{
    int part = 0;
    if (buffer[1] & 0x80) { // binary
        part = buffer[2];
        if (len != 3) {
            return ERR_SYNTAX;
        }
    } else {
        if (sscanf((const char *)buffer + 2, "%d", &part) != 1) {
            return ERR_SYNTAX;
        }
    }
    return do_set_current_partition(part);
}

int dir_command(const uint8_t *buffer, int len)
{
    if (buffer[1] != 'D') {
        return ERR_UNKNOWN_CMD;
    }
    mstring cmd((const char *)buffer, 2, len-1);    
    filename_t dest;
    int err = parse_full_path(cmd.c_str(), dest, NULL, true);
    if (err) {
        return err;
    }
    mstring path = dest.path;
    if ((path.length() > 0) && (path[-1] != '/') && (dest.filename.length() > 0)) {
        path += '/';
    }
    path += dest.filename;
    const char *mode;
    switch (buffer[0]) {
    case 'C': return do_change_dir(dest.partition, path);
    case 'M': return do_make_dir(dest.partition, path);
    case 'R': return do_remove_dir(dest.partition, path);
    default:
        return ERR_UNKNOWN_CMD;
    }
    return 0;
}

int copy_command(const uint8_t *buffer, int len)
{
    // [[n][path]:]filename = commalist([[n][path]:]pattern)    
    // Split on =, then parse before = as [[n][path]:]filename
    while(isalpha(*buffer)) {
        buffer++;
        len--;
    }
    mstring cmd((const char *)buffer, 0, len-1);    
    const char *remaining;
    if (!cmd.split('=', &remaining)) {
        return ERR_SYNTAX;
    }
    filename_t dest;
    int err = parse_full_path(cmd.c_str(), dest);
    if (err) {
        return err;
    }
    if (dest.has_wildcard)
        return ERR_ILLEGAL_NAME;

    filename_t source_list[8];
    const char *sources[8] = { NULL };
    mstring src(remaining);
    int n = src.split(',', sources, 8);
    for(int i = 0; i < n; i ++) {
        err = parse_full_path(sources[i], source_list[i]);
        if (err) {
            return err;
        }
    }
    return do_copy(dest, source_list, n);
}

int initialize_command(const uint8_t *buffer, int len)
{
    return do_initialize();
}

int format_command(const uint8_t *buffer, int len)
{
    uint8_t name[24] = { 0xA0 };
    name[23] = 0;
    uint8_t id1 = 0xA0, id2 = 0xA0;
    for(int i=2;i<len;i++) {
        if (buffer[i] == ',') {
            id1 = (i+1 < len)?buffer[i+1]:0xA0;
            id2 = (i+2 < len)?buffer[i+2]:0xA0;
            break;
        } else {
            name[i-2] = buffer[i];
        }
    }
    return do_format(name, id1, id2);
}

int position_command(const uint8_t *buffer, int len)
{
    uint32_t pos = 0;
    len--;

    if (len > 5) {
        return ERR_SYNTAX;
    }
    int chan = (int)buffer[1];
    for(int i=0; i<len-1; i++) {
        pos |= ((uint32_t)buffer[i+2]) << (8*i);
    }
    return do_set_position(chan, pos);
}

int rename_command(const uint8_t *buffer, int len)
{
    while(isalpha(*buffer)) {
        buffer++;
        len--;
    }
    mstring cmd((const char *)buffer, 0, len-1);
    const char *remaining;
    if (!cmd.split('=', &remaining)) {
        return ERR_SYNTAX;
    }
    filename_t dest;
    int err = parse_full_path(cmd.c_str(), dest, NULL);
    if (err) {
        return err;
    }
    filename_t src;
    err = parse_full_path(remaining, src, NULL);
    if (err) {
        return err;
    }

    if (dest.has_wildcard)
        return ERR_ILLEGAL_NAME;

    return do_rename(src, dest);
}

int scratch_command(const uint8_t *buffer, int len)
{
    while(isalpha(*buffer)) {
        buffer++;
        len--;
    }
    mstring cmd((const char *)buffer, 0, len-1);
    filename_t filenames[8];
    const char *files[8] = { NULL };
    int n = cmd.split(',', files, 8);
    for(int i = 0; i < n; i ++) {
        int err = parse_full_path(files[i], filenames[i], NULL);
        if (err) {
            return err;
        }
    }
    return do_scratch(filenames, n);
}

uint8_t bcdbyte(int a)
{
    if(a > 99)
        return 0x99;
    uint8_t r = (a / 10) << 4 | (a % 10);
    return r;
}

int time_command(const uint8_t *buffer, int len)
{
    const char *wd4[] = { "SUN.", "MON.", "TUES", "WED.", "THUR", "FRI.", "SAT." };
    const char *wd3[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    uint8_t result[32];
    if (len != 4) {
        return ERR_SYNTAX;
    }
    if (buffer[1] != '-') {
        return ERR_SYNTAX;
    }
    int wd, day, month, year, hour, min, sec, hour12;
    // get time
    wd = 3; year = 2025; month = 6; day = 26; hour = 0; min = 41; sec = 1;
    hour12 = (hour % 12); if (hour12 == 0) hour12 = 12;

    int reslen;
    switch(buffer[2]) {
    case 'R':
        switch(buffer[3]) {
        case 'A': // ASC-II in wrong order date format
            // "dow. mo/da/yr hr:mi:se xx"+CHR$(13)
            reslen = sprintf((char *)result, "%s %02d/%02d/%02d %02d:%02d:%02d %s\r",
                wd4[wd], day, month, year % 100, hour12, min, sec, (hour >= 12)?"PM":"AM");  
            break;
        case 'D': // In binary form
            // wd, yr, mon, day, hr12, min, sec, flag, 0D
            reslen = 9;
            result[0] = (uint8_t)wd;
            result[1] = (uint8_t)(year-1900);
            result[2] = (uint8_t)month;
            result[3] = (uint8_t)day;
            result[4] = (uint8_t)hour12;
            result[5] = (uint8_t)min;
            result[6] = (uint8_t)sec;
            result[7] = (hour >= 12)?1:0;
            result[8] = 0x0d;
            break;
        case 'B': // in BCD format
            reslen = 9;
            result[0] = (uint8_t)wd;
            result[1] = bcdbyte(year%100);
            result[2] = bcdbyte(month);
            result[3] = bcdbyte(day);
            result[4] = bcdbyte(hour12);
            result[5] = bcdbyte(min);
            result[6] = bcdbyte(sec);
            result[7] = (hour >= 12)?1:0;
            result[8] = 0x0d;
            break;
        case 'I': // in ISO format
            // "YYYY-MM-DDThh:mm:ss dow"+CHR$(13)
            reslen = sprintf((char *)result, "%04d-%02d-%02dT%02d:%02d:%02d %s\r", year, month, day, hour, min, sec, wd3[wd]);
            break;
        default:
            return ERR_SYNTAX; 
        }
        return do_cmd_response(result, reslen);
        break;
    case 'W':
        return 0; // just assume OK
    }
    return ERR_UNKNOWN_CMD;
}

int user_command(const uint8_t *buffer, int len)
{
    int n, chan, part, track, sector;
    switch(buffer[1]) {
    case '1':
        n = sscanf((const char *)buffer+2, "%d%d%d%d", &chan, &part, &track, &sector);
        if (n != 4) return ERR_SYNTAX;
        return do_block_read(chan, part, track, sector);
    case '2':
        n = sscanf((const char *)buffer+2, "%d%d%d%d", &chan, &part, &track, &sector);
        if (n != 4) return ERR_SYNTAX;
        return do_block_write(chan, part, track, sector);
    default:
        return ERR_UNKNOWN_CMD;
    }
    return 0;
}

int execute_command(const uint8_t *buffer, int len)
{
    switch(buffer[0]) {
    case 'B': return block_command(buffer, len);
    case 'C': 
        switch(buffer[1]) {
        case 'D': return dir_command(buffer, len);
        case 'P':
        case 0xD0: return cp_command(buffer, len);
        default:
            return copy_command(buffer, len);
        }
        break;
    case 'I': return initialize_command(buffer, len);
    case 'M': return dir_command(buffer, len);
    case 'N': return format_command(buffer, len);
    case 'P': return position_command(buffer, len);
    case 'R':
        if (buffer[1] == 'D') {
            return dir_command(buffer, len);
        }
        return rename_command(buffer, len);
    case 'S': return scratch_command(buffer, len);
    case 'T': return time_command(buffer, len);
    case 'U': return user_command(buffer, len);
    }
    return ERR_UNKNOWN_CMD;
}

const char *cbmdos_time(uint32_t dt, char *buf, bool longfmt)
{
    int y, M, D, h, m, s;
    y = 1980 + (dt >> 25);
    M = (dt >> 21) & 15;
    D = (dt >> 16) & 31;
    h = (dt >> 11) & 31;
    m = (dt >> 5) & 63;
    s = (dt & 31) << 1;
    int h12 = (h % 12); if (h12 == 0) h12 = 12;
    if (longfmt) {
        // 07/27/19 03.44 PM
        sprintf(buf, "%02d/%02d/%02d %02d.%02d %cM", M, D, y % 100, h12, m, (h >= 12)?'P':'A');
    } else {
        // 07/27 03.44 P
        sprintf(buf, "%02d/%02d %02d.%02d %c", M, D, h12, m, (h >= 12)?'P':'A');
    }
    return buf;
}

const uint32_t make_fat_time(int y, int M, int d, int h, int m, int s)
{
    y -= 1980;
    uint32_t result = 0;
    result |= (y << 25);
    result |= (M << 21);
    result |= (d << 16);
    result |= (h << 11);
    result |= (m << 5);
    result |= (s >> 1);

    return result;
}
