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
// T-WA, T-WB, T-WD, T-WI -> set the drive's own clock from the same layout the matching
//   read answers with, as an offset from the system clock; the system clock is not
//   written (SI-120).

// Directories
// $ [=T] [[n][path]:] [= commalist([TP|OPTION])], OPTION = { L, N, <stamp, >stamp }, stamp = MM/DD/YY HH:MM xM, x = {A | P}
// $ [=P] [:pattern] -> outputs a listing of current partitions

// Swaplists are not part of the drive, they are part of the user interface.

// Block commands should not be available outside of a CBM image, as you cannot expect a commodore program to know about
// the underlying file system. However, the B-A and B-F commands should be implemented within an image, and the buffer access
// methods should work.

#include "cbmdos_parser.h"
#include "current_time.h"

// A build with no real time clock driver, which is every host test build, reads a
// fixed system clock, so that the clock commands can still be exercised end to end.
extern "C" {
    void get_current_time(int& wd, int& year, int& month, int& day, int& hour, int& min, int& sec) __attribute__((weak));
    void get_current_time(int& wd, int& year, int& month, int& day, int& hour, int& min, int& sec)
    {
        wd = 3; year = 2025; month = 6; day = 26;
        hour = 0; min = 41; sec = 1;
    }
}
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
        // Skip whitespace before partition number
        while(buf[idx] == ' ') {
            idx++;
        }
        if (isdigit(buf[idx])) {
            name.partition = 0;
            while(isdigit(buf[idx])) {
                name.partition *= 10;
                name.partition += (buf[idx++] - '0');
                if (name.partition > 0xFFFF) { // no partition number is that large
                    name.partition = 0xFFFF;
                }
            }
        }
        // Skip whitespace after partition number
        while(buf[idx] == ' ') {
            idx++;
        }
        // Paths should start with a slash or underscore, if any.
        if ((buf[idx] == '/') || (path_only && buf[idx] == '_')) {
            if (colon) {
                int colon_pos = colon - buf;
                name.path.copy(buf, idx, colon_pos-1);
            } else { // there is no colon, so we do path only
                name.path = buf + idx;
            }
        } else if (!path_only && buf[idx] != ':') { // other non-zero char: error
            //printf("Syntaxing out, expecting :, but found %b: '%s' (P=%d)\n", buf[idx], buf, name.partition);
            return ERR_SYNTAX;
        }
    } else { // no colon
        name.filename = buf;
    }
    name.has_wildcard = name.filename.contains_any("?*");
    if (!path_only && name.filename.length() == 0) {
        return ERR_NO_NAME;
    }
    return 0;
}

int parse_dir_option(const char *buf, dir_options_t &opt)
{
    int M, d, y, h, h12, m, n;
    char ampm;
    uint32_t datetime;

    switch(buf[0]) {
    //case 'S': opt.timefmt = e_stamp_short; break;
    case 'L': opt.timefmt = e_stamp_long;  break;
    case 'P': opt.filetypes |= 0x02; break;
    case 'S': opt.filetypes |= 0x04; break;
    case 'U': opt.filetypes |= 0x08; break;
    case 'R': opt.filetypes |= 0x10; break;
    case 'B': opt.filetypes |= 0x20; break;
    case 'D': opt.filetypes |= 0x20; break;
    case 'H': opt.show_hidden = true; break; // a flag, not a file type (SI-134)
    case 'N': opt.timefmt = e_stamp_none; break;
    case '<':
    case '>':
        // MM/DD/YY HH:MM xM, with x either A or P, as in $:*=>01/02/25 03:04 PM.
        n = sscanf(buf+1, "%d/%d/%d %d:%d %c", &M, &d, &y, &h12, &m, &ampm);
        if ((n != 6) || ((ampm != 'A') && (ampm != 'P')) || (M < 1) || (M > 12) ||
            (d < 1) || (d > 31) || (h12 < 1) || (h12 > 12) || (m < 0) || (m > 59)) {
            return ERR_SYNTAX; // a field out of range would run into its neighbour's bits
        }
        {
            const char *mark = strchr(buf + 1, ampm);
            if (!mark || (mark[1] != 'M')) {
                return ERR_SYNTAX;
            }
        }
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

// The partition directory takes a type where a directory of files takes its
// options: LOAD"$=P[:*][=tp]" with tp one of N, 4, 7, 8 or C. The bit set here is
// the CMD partition type code, so the drive can test it against a partition's own
// type without a second table.
int parse_partition_option(const char *buf, dir_options_t &opt)
{
    switch(buf[0]) {
    case 'N': opt.partition_types |= (1 << 1); break; // native, and a DNP image
    case '4': opt.partition_types |= (1 << 2); break; // 1541 image
    case '7': opt.partition_types |= (1 << 3); break; // 1571 image
    case '8': opt.partition_types |= (1 << 4); break; // 1581 image
    case 'C': opt.partition_types |= (1 << 5); break; // 1581 CP/M, which this drive has none of
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
    fn.record_size = 0;
    fn.buffers = 0;

    int err = 0;
    bool record_length_given = false;
    if (buf[0] == '#') {
        fn.dir_opt.stream = e_stream_buffer;
        // "##n", exactly three characters, asks for n chained buffers with the pointer at
        // byte 0; any other name after the # is a standard buffer, whose pointer starts
        // at byte 1 (SI-090, SD open_buffer()).
        if ((buf[1] == '#') && isdigit(buf[2]) && !buf[3]) {
            fn.buffers = (uint8_t)(buf[2] - '0');
        }
        // Call parse to initialize the rest
        err = parse_full_path(buf+1, fn.file, NULL, true);
    } else if (buf[0] == '$') {
        fn.dir_opt.stream = e_stream_dir;
        if (buf[1] == '=') {
            if (buf[2] == 'P') {
                fn.dir_opt.stream = e_stream_partitions;
            } else if(buf[2] == 'T') {
                fn.dir_opt.timefmt = e_stamp_short;
            }
            err = parse_full_path(buf+3, fn.file, &fn.replace, true);
        } else {
            err = parse_full_path(buf+1, fn.file, &fn.replace, true);
        }
    } else {
        // The record length of a relative file is the byte after ",L,", whatever byte
        // that is, so it is taken off before a comma or a colon in it is read as syntax
        // (SI-080, SD file_open()).
        const char *rel = strstr(buf, ",L,");
        if (rel && rel[3] && !rel[4]) {
            mstring name(buf, 0, (int)(rel - buf) - 1);
            fn.record_size = (uint8_t)rel[3];
            record_length_given = true;
            err = parse_full_path(name.c_str(), fn.file, &fn.replace);
        } else {
            err = parse_full_path(buf, fn.file, &fn.replace);
        }
    }
    if (err) {
        return err;
    }

    if (fn.dir_opt.stream == e_stream_buffer) {
        return 0;
    }

    if (fn.dir_opt.stream != e_stream_file) {
        // Parse options / filter
        const char *rem;
        if (fn.file.filename.split('=', &rem)) {
            mstring opts(rem);
            const char *parts[6] = { NULL };
            int n = opts.split(',', parts, 6);
            for (int i=0;i<n;i++) {
                err = (fn.dir_opt.stream == e_stream_partitions)
                    ? parse_partition_option(parts[i], fn.dir_opt)
                    : parse_dir_option(parts[i], fn.dir_opt);
                if (err) return err;
            }
        }
        return 0;
    }

    // check for file access modifiers
    const char *modifiers[3] = { NULL };
    int m = 0;
    if (record_length_given) {
        fn.filetype = e_rel;
    } else {
        m = fn.file.filename.split(',', modifiers, 3);
    }
    for(int i=1; i < m; i++) {
        switch (modifiers[i][0]) {
        case 'R': fn.access = e_read; break;
        case 'W': fn.access = e_write; break;
        case 'A': fn.access = e_append; break;
        // Modify reads a file a write never closed, which is a read here: the splat is
        // in the directory entry and nothing refuses to read such a file (SI-070).
        case 'M': fn.access = e_read; break;
        case 'P': fn.filetype = e_prg; break;
        case 'S': fn.filetype = e_seq; break;
        case 'U': fn.filetype = e_usr; break;
        case 'L':
            if (i == 1) {
                fn.filetype = e_rel;
                if (modifiers[2]) {
                    fn.record_size = (uint8_t)modifiers[2][0];
                } // if not set, it will be zero
                i++;
            } else {
                return ERR_SYNTAX;
            }
            break;
        default:
            return ERR_SYNTAX;
        }
    }

    // A name that starts with a shifted space is no name to create: 64 with @, as for a
    // pattern (SI-148, SD file_open()).
    if (fn.replace && ((uint8_t)fn.file.filename.c_str()[0] == 0xA0)) {
        return ERR_REPLACE_TYPE;
    }
    // A shifted space is a legal byte inside a name (SI-148); only a name that starts
    // with one is refused, and only when a file is to be created (setup_file_access()).
    if (fn.file.filename.contains_any(",=:\r")) {
        return ERR_ILLEGAL_NAME;
    }
    return 0;
}

// The parameters of a block command are decimal numbers separated by a space, a
// comma or a cursor right (0x1D), and a colon may stand between the command word and
// the first parameter. The 1541 ROM routine at $CC6F skips exactly those.
//
// A Commodore rarely sends the bare form: PRINT#15,"U1:";2;0;18;0 puts
// "U1: 2  0  18  0 " on the bus, because BASIC prints a space before and after every
// positive number, and the "VIEW BAM" program on the 1541 TEST/DEMO disk sends the
// literal forms "U1:2,0,18,0" and "B-P:2,144".
static bool is_block_parameter_separator(uint8_t c, bool before_first)
{
    return (c == ' ') || (c == ',') || (c == 0x1D) || (before_first && (c == ':'));
}

static int parse_block_parameters(const uint8_t *buffer, int len, int *values, int count)
{
    int found = 0;
    int i = 0;

    while (found < count) {
        while ((i < len) && is_block_parameter_separator(buffer[i], found == 0)) {
            i++;
        }
        if ((i >= len) || !isdigit(buffer[i])) {
            break;
        }
        uint32_t value = 0;
        while ((i < len) && isdigit(buffer[i])) {
            value = (value * 10) + (uint32_t)(buffer[i] - '0');
            if (value > 0xFFFF) { // CBM DOS keeps a parameter in sixteen bits
                value = 0xFFFF;
            }
            i++;
        }
        values[found++] = (int)value;
    }
    return found;
}

int IecParser :: block_command(const uint8_t *buffer, int len)
{
    // B-R and BLOCK-READ are one command: the letter after the dash names it, as the
    // 1541 ROM and SD parse_block() read it (SI-091).
    int dash = 1;
    while ((dash < len) && isalpha(buffer[dash])) {
        dash++;
    }
    if ((dash >= len - 1) || (buffer[dash] != '-')) {
        return ERR_SYNTAX;
    }
    uint8_t letter = buffer[dash + 1];
    int start = dash + 2;
    while ((start < len) && isalpha(buffer[start])) {
        start++;
    }
    int n;
    int p[4];
    const uint8_t *params = buffer + start;
    int param_len = len - start;

    switch(letter) {
    case 'R':
        n = parse_block_parameters(params, param_len, p, 4);
        if (n != 4) return ERR_SYNTAX;
        return exec->do_block_read(p[0], p[1], p[2], p[3], true);
    case 'W':
        n = parse_block_parameters(params, param_len, p, 4);
        if (n != 4) return ERR_SYNTAX;
        return exec->do_block_write(p[0], p[1], p[2], p[3], true);
    case 'P':
        n = parse_block_parameters(params, param_len, p, 3);
        if (n < 2) return ERR_SYNTAX;
        // Two numbers are the 1541's eight bit position, which keeps the low byte of
        // what it is given. A third is the high byte of a sixteen bit position
        // (SI-092, SD README).
        return exec->do_buffer_position(p[0], (n >= 3) ? (p[1] + (p[2] << 8)) : (p[1] & 0xFF));
    case 'A':
    case 'F':
        // Allocate and free take the partition, the track and the sector. They do
        // not take a channel, because they touch no buffer; the manuals write them
        // as B-A:"drive;track;block. A fourth number is what the ROM's parameter
        // reader would have collected and the command would then have ignored.
        n = parse_block_parameters(params, param_len, p, 4);
        if (n < 3) return ERR_SYNTAX;
        return exec->do_block_allocate(p[0], p[1], p[2], letter == 'A');
    default:
        return ERR_SYNTAX;
    }
    return 0;
}

int IecParser :: cp_command(const uint8_t *buffer, int len)
{
    int part = 0;
    if (buffer[1] & 0x80) { // binary
        // C<shift-P> always carries its partition number, so anything after it is
        // the terminator BASIC appends and not a second parameter.
        if (len < 3) {
            return ERR_SYNTAX;
        }
        part = buffer[2];
    } else {
        // Partition 0 means "the one already selected", which is also what a command
        // with no number at all asks for.
        if (parse_block_parameters(buffer + 2, (len > 2) ? (len - 2) : 0, &part, 1) != 1) {
            return exec->do_set_current_partition(0);
        }
    }
    return exec->do_set_current_partition(part);
}

int IecParser :: dir_command(const uint8_t *buffer, int len)
{
    if (buffer[1] != 'D') {
        return ERR_SYNTAX;
    }
    mstring cmd((const char *)buffer, 2, len-1);    
    const char *colon = strchr(cmd.c_str(), ':');
    if (buffer[0] == 'M') {
        // MD requires a colon, and a name that starts with a shifted space is no name
        // (SI-060, SD parse_mkdir()).
        if (!colon || ((uint8_t)colon[1] == 0xA0)) {
            return ERR_NO_NAME;
        }
    } else if (buffer[0] == 'R') {
        // RD takes a partition number and a name behind a colon, and no path, so that a
        // directory cannot be removed from inside it (SI-063, HD 9-19, SD parse_rmdir()).
        const char *p = cmd.c_str();
        while ((*p == ' ') || isdigit(*p)) {
            p++;
        }
        if (strchr(cmd.c_str(), '/') || (*p != ':')) {
            return ERR_NO_NAME;
        }
    }
    filename_t dest;
    int err = parse_full_path(cmd.c_str(), dest, NULL, true);
    if (err) {
        return err;
    }
    if (buffer[0] != 'C') {
        // A colon with nothing after it is no name (SI-030).
        int nlen = dest.filename.length();
        if (!nlen) {
            return ERR_NO_NAME;
        }
        // A directory to be made cannot carry a wildcard, and a FAT host drops a
        // trailing dot or space from the name it creates, which could then not be
        // found again (SI-141).
        char last = dest.filename.c_str()[nlen - 1];
        if ((buffer[0] == 'M') && (dest.has_wildcard || (last == '.') || (last == ' '))) {
            return ERR_ILLEGAL_NAME;
        }
    }
    switch (buffer[0]) {
    case 'C': return exec->do_change_dir(dest);
    case 'M': return exec->do_make_dir(dest);
    case 'R': return exec->do_remove_dir(dest);
    default:
        return ERR_SYNTAX;
    }
    return 0;
}

static int parse_name_list(const char *buf, filename_t *&names, int *count);

int IecParser :: copy_command(const uint8_t *buffer, int len)
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

    filename_t *source_list = NULL;
    int n = 0;
    err = parse_name_list(remaining, source_list, &n);
    if (!err) {
        err = IecParser :: exec->do_copy(dest, source_list, n);
    }
    delete[] source_list;
    return err;
}

int IecParser :: get_command(const uint8_t *buffer, int len)
{
    if (buffer[1] != '-') {
        return ERR_SYNTAX;
    }
    int n, chan, part, track, sector;
    switch(buffer[2]) {
    case 'P':
        if (len == 4) {
            // 255 asks for the current partition, which is what no parameter asks for
            // as well, and is passed on as -1. Partition 0 is the system partition, a
            // different question (SI-041).
            int wanted = (int)buffer[3];
            return exec->do_get_partition_info((wanted == 255) ? -1 : wanted);
        } else if (len == 3) {
            return exec->do_get_partition_info(-1);
        }
        return ERR_SYNTAX;
    default:
        return ERR_SYNTAX;
    }
    return 0;
}

// I[n][:] initialises the medium. UI is a different command, a reset that answers
// with the DOS version, and is handled by user_command().
int IecParser :: initialize_command(const uint8_t *buffer, int len)
{
    return exec->do_initialize_buffers();
}

// The name and the optional id of a command that names a directory header or an image:
// [n][path]:name[,id]. The id is whatever follows the first comma, and a name of no
// characters is no name at all.
int IecParser :: name_and_id(const char *arg, filename_t& dest, const char *&id)
{
    id = "";
    int err = parse_full_path(arg, dest, NULL, false);
    if (err) {
        return err;
    }
    const char *rest;
    if (dest.filename.split(',', &rest)) {
        id = rest;
    }
    if (dest.filename.length() == 0) {
        return ERR_NO_NAME;
    }
    return 0;
}

// N[n][path]:name[,id] creates or formats a disk image (SI-071, SD parse_new()). The
// name needs a colon in front of it, and is split from the id at the first comma.
int IecParser :: format_command(const uint8_t *buffer, int len)
{
    mstring cmd((const char *)buffer, 1, len-1);    
    if (!strchr(cmd.c_str(), ':')) {
        return ERR_NO_NAME;
    }
    filename_t dest;
    const char *id;
    int err = name_and_id(cmd.c_str(), dest, id);
    if (err) {
        return err;
    }
    return exec->do_format(dest, id);
}

// P+CHR$(ch)+CHR$(lo)[+CHR$(hi)[+CHR$(offset)]] for a relative file, and up to four
// position bytes for any other file (SD README). The record number and offset are read
// from the command as sent, len, because a 13 in the offset's place is the offset
// (SI-018, 1541 ROM $E23E). The position is read from the command without its terminator,
// stripped_len, so a position of fewer than four bytes from BASIC does not take the
// carriage return PRINT# appends as its next byte.
int IecParser :: position_command(const uint8_t *buffer, int len, int stripped_len)
{
    uint32_t pos = 0;
    int chan = (int)buffer[1];
    len -= 2; stripped_len -= 2; buffer += 2;

    if (len < 1) {
        return ERR_SYNTAX;
    }
    for(int i=0; (i < stripped_len) && (i < 4); i++) {
        pos |= ((uint32_t)buffer[i]) << (8*i);
    }
    int recnr = buffer[0] | ((len >= 2) ? (buffer[1] << 8) : 0);
    int recoffset = (len >= 3) ? buffer[2] : 0;
    return exec->do_set_position(chan, pos, recnr, recoffset);
}

int IecParser :: rename_command(const uint8_t *buffer, int len)
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
    if ((uint8_t)dest.filename.c_str()[0] == 0xA0) {
        return ERR_NO_NAME; // a name that starts with a shifted space (SI-148, SD parse_rename())
    }

    return exec->do_rename(src, dest);
}

// R-P:newname=oldname renames a partition and R-H[n][path]:newname[,id] renames the
// header of a directory (SI-051, SI-064). SD parse_doscommand() finds both the same
// way, by the dash in the second character, before it falls through to a file rename.
int IecParser :: rename_dashed_command(const uint8_t *buffer, int len)
{
    mstring cmd((const char *)buffer, 3, len-1);
    switch (buffer[2]) {
    case 'P': {
        const char *rest;
        if (!cmd.split('=', &rest)) {
            return ERR_SYNTAX;
        }
        const char *newname = cmd.c_str();
        if (*newname == ':') {
            newname++;
        }
        if (!*newname || !*rest) {
            return ERR_NO_NAME;
        }
        return exec->do_rename_partition(newname, rest);
    }
    case 'H':
        return header_command(cmd.c_str());
    default:
        return ERR_SYNTAX;
    }
}

// A comma separated list of names, each of the form [[n][path]:]pattern, as scratch, copy
// and the sd2iec attribute commands take it. The list is as long as the command makes it
// (SI-150), so it is allocated here, and the caller deletes it whatever the answer.
static int parse_name_list(const char *buf, filename_t *&names, int *count)
{
    int n = 1;
    for (const char *p = buf; *p; p++) {
        if (*p == ',') {
            n++;
        }
    }
    mstring cmd(buf);
    const char **parts = new const char *[n];
    n = cmd.split(',', parts, n);
    names = new filename_t[n];
    int err = 0;
    for (int i = 0; (i < n) && !err; i++) {
        err = parse_full_path(parts[i], names[i], NULL);
    }
    delete[] parts;
    *count = n;
    return err;
}

int IecParser :: scratch_command(const uint8_t *buffer, int len)
{
    while(isalpha(*buffer)) {
        buffer++;
        len--;
    }
    mstring cmd((const char *)buffer, 0, len-1);
    filename_t *filenames = NULL;
    int n = 0;
    int err = parse_name_list(cmd.c_str(), filenames, &n);
    if (!err) {
        err = exec->do_scratch(filenames, n);
    }
    delete[] filenames;
    return err;
}

// S-8, S-9 and S-D, exactly three characters, change the device number to 8, to 9 and
// back to the configured one (SI-101, HD 9-34). On a CMD device they swap the drive the
// number addresses; here there is one drive, so they move that drive's number. Any other
// name after the S is a scratch, so a file named "-8" is scratched as S:-8.
int IecParser :: swap_command(const uint8_t *buffer, int len)
{
    switch (buffer[2]) {
    case '8': return exec->do_set_device_number(8);
    case '9': return exec->do_set_device_number(9);
    case 'D': return exec->do_restore_device_number();
    default:  return ERR_SYNTAX; // S is a command letter, its argument is not (SI-030)
    }
}

// L[n][path]:name toggles the lock of one file or directory (SI-076, HD 9-30).
int IecParser :: lock_command(const uint8_t *buffer, int len)
{
    mstring cmd((const char *)buffer, 1, len-1);
    filename_t name;
    int err = parse_full_path(cmd.c_str(), name, NULL, false);
    if (err) {
        return err;
    }
    return exec->do_toggle_attributes(name, IEC_ATTR_LOCKED);
}

// [n][path]:name[,id], the argument of every command that sets a directory header:
// R-H (SI-064) and the sd2iec spellings EH, XH and D (SI-077).
int IecParser :: header_command(const char *arg)
{
    if (!strchr(arg, ':')) {
        return ERR_NO_NAME; // the name follows a colon, as for N
    }
    filename_t dest;
    const char *id;
    int err = name_and_id(arg, dest, id);
    if (err) {
        return err;
    }
    if (dest.filename.contains_any("?*")) {
        return ERR_ILLEGAL_NAME;
    }
    return exec->do_set_header(dest, id);
}

// The sd2iec attribute commands (SI-077). EL and EU set and clear the lock on every
// entry each name matches, EH turns the hidden flag of one entry over, and
// A:[R][H][A]=name sets exactly the attributes named. The header forms EH with a colon
// straight after the partition, XH and D are the same command as R-H.
// Sources: SD parse_elock(), parse_eunlock(), parse_ehide(), parse_attr(),
// parse_set_header() and the dispatch in parse_doscommand().
int IecParser :: attribute_command(const uint8_t *buffer, int len)
{
    mstring cmd((const char *)buffer, 0, len-1);
    const char *arg2 = cmd.c_str() + 2; // behind a two character command word
    filename_t *names = NULL;
    int n = 0;
    int err;

    switch (buffer[0]) {
    case 'A':
        // A:[R][H][A]=name, whose letters are every attribute the entry is to carry.
        if (buffer[1] != ':') {
            return ERR_UNKNOWN_CMD;
        }
        {
            const char *rest;
            mstring letters(arg2);
            if (!letters.split('=', &rest)) {
                return ERR_UNKNOWN_CMD; // SD parse_attr() answers 31 without the =
            }
            uint8_t attrib = 0;
            for (const char *p = letters.c_str(); *p; p++) {
                switch (*p) {
                case 'R': attrib |= IEC_ATTR_LOCKED; break;
                case 'H': attrib |= IEC_ATTR_HIDDEN; break;
                case 'A': attrib |= IEC_ATTR_ARCHIVE; break;
                default: return ERR_SYNTAX;
                }
            }
            err = parse_name_list(rest, names, &n);
            if (!err) {
                err = exec->do_set_attributes(names, n, attrib,
                                              IEC_ATTR_LOCKED | IEC_ATTR_HIDDEN | IEC_ATTR_ARCHIVE);
            }
            delete[] names;
            return err;
        }
    case 'D':
        if (buffer[1] != ':') {
            return ERR_SYNTAX; // the sd2iec direct sector commands are out of scope (SI-096)
        }
        return header_command(cmd.c_str() + 1);
    case 'E':
    case 'X':
        switch (buffer[1]) {
        case 'L':
        case 'U':
            err = parse_name_list(arg2, names, &n);
            if (!err) {
                err = exec->do_set_attributes(names, n,
                                              (buffer[1] == 'L') ? IEC_ATTR_LOCKED : 0,
                                              IEC_ATTR_LOCKED);
            }
            delete[] names;
            return err;
        case 'H': {
            if (buffer[0] == 'X') {
                // XH+ and XH- are the setting that adds hidden files to every listing.
                // This drive keeps its settings in the Ultimate configuration and takes
                // the request per listing instead, as =H (SI-134).
                if (len == 3) {
                    return ERR_SYNTAX;
                }
                return header_command(arg2);
            }
            // After an EH, a colon straight after the partition number is the header
            // form; anything else names one entry whose hidden flag is turned over.
            const char *p = arg2;
            while ((*p == ' ') || isdigit(*p)) {
                p++;
            }
            if (*p == ':') {
                return header_command(arg2);
            }
            filename_t name;
            err = parse_full_path(arg2, name, NULL, false);
            if (err) {
                return err;
            }
            return exec->do_toggle_attributes(name, IEC_ATTR_HIDDEN);
        }
        case 'P':
            if (cmd == "XPWD") {
                return exec->do_pwd_command();
            }
            return ERR_SYNTAX;
        default:
            return ERR_SYNTAX;
        }
    }
    return ERR_SYNTAX;
}

// M-R (SI-105). This drive has no drive memory, so M-R answers the number of bytes
// asked for, every one of them $00, which is no drive's signature (SI-112). M-W and M-E
// are refused: answering OK would tell a program that its drive code is in place. The
// exception is an M-W to the listen address, which changes the device number (SI-100a).
int IecParser :: memory_command(const uint8_t *buffer, int len)
{
    switch(buffer[2]) {
    case 'R': {
        if (len < 5) {
            return ERR_SYNTAX;
        }
        // No count reads one byte, as the 1541 ROM does at $CB24; a count of zero is
        // 256; and a read stops at the end of the page.
        int count = (len >= 6) ? buffer[5] : 1;
        if (count == 0) {
            count = 256;
        }
        if (count > 256 - buffer[3]) {
            count = 256 - buffer[3];
        }
        uint8_t zeros[256];
        memset(zeros, 0, sizeof(zeros));
        return exec->do_cmd_response(zeros, count);
    }
    case 'E':
        // No drive code runs here, so every address holds a code this drive does not
        // know, which is what sd2iec answers when it recognises none (SI-105).
        return ERR_UNKNOWN_DRIVECODE;
    case 'W':
        // The 1541 keeps its listen address at $0077, and a CMD drive's SWAP button
        // writes it to move the other drive; its low five bits are the number.
        if ((len >= 7) && (buffer[3] == 0x77) && (buffer[4] == 0x00) && (buffer[5] != 0)) {
            int dev = buffer[6] & 0x1F;
            if ((dev >= 8) && (dev <= 30)) {
                return exec->do_set_device_number(dev);
            }
        }
        return ERR_SYNTAX;
    default:
        return ERR_SYNTAX;
    }
}

static uint8_t bcdbyte(int a)
{
    if(a > 99)
        return 0x99;
    uint8_t r = (a / 10) << 4 | (a % 10);
    return r;
}

// The day of week names a clock answer carries, which a clock write is also matched
// against, so the two cannot name the days differently.
static const char *const c_weekday_4[] = { "SUN.", "MON.", "TUES", "WED.", "THUR", "FRI.", "SAT." };
static const char *const c_weekday_3[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

// The clock chip holds two BCD digits of year counted from 1980, so a write outside
// that century is refused rather than wrapped.
#define CLOCK_FIRST_YEAR 1980
#define CLOCK_LAST_YEAR  2079

// The time a clock write carries, before it is checked.
typedef struct {
    int wd, year, month, day, hour, min, sec;
} clock_time_t;

// A field of decimal digits at a fixed place in a clock command, or -1 when it is not
// digits. The formats place every field and every separator at a fixed offset, and the
// A format's AM or PM marker is at a fixed offset even in SD parse_timewrite(), so a
// field of another width could not be read consistently and is refused.
static int clock_field(const uint8_t *p, int digits)
{
    int value = 0;
    for (int i = 0; i < digits; i++) {
        if (!isdigit(p[i])) {
            return -1;
        }
        value = (value * 10) + (p[i] - '0');
    }
    return value;
}

static int clock_bcd(uint8_t b)
{
    if (((b & 0x0F) > 9) || ((b >> 4) > 9)) {
        return -1;
    }
    return ((b >> 4) * 10) + (b & 0x0F);
}

// A two digit year is in this century below 80 and in the last one from 80, which is
// the Y2K rule of SD parse_timewrite().
static int clock_year_of(int two_digit)
{
    return (two_digit < 80) ? (2000 + two_digit) : (1900 + two_digit);
}

static int clock_days_in_month(int year, int month)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if ((month == 2) && (((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0)))) {
        return 29;
    }
    return days[month - 1];
}

// The day of week of a date, 0 for Sunday, by the method SD parse_timewrite() uses for
// the ISO form, which is the one form that carries no day of week.
static int clock_day_of_week(int year, int month, int day)
{
    int y = (month < 3) ? (year - 1) : year;
    int d = day + ((month < 3) ? year : (year - 2));
    return ((23 * month / 9) + d + 4 + (y / 4) - (y / 100) + (y / 400)) % 7;
}

static bool clock_time_valid(const clock_time_t& t)
{
    return (t.year >= CLOCK_FIRST_YEAR) && (t.year <= CLOCK_LAST_YEAR) &&
           (t.month >= 1) && (t.month <= 12) &&
           (t.day >= 1) && (t.day <= clock_days_in_month(t.year, t.month)) &&
           (t.wd >= 0) && (t.wd <= 6) &&
           (t.hour >= 0) && (t.hour <= 23) &&
           (t.min >= 0) && (t.min <= 59) &&
           (t.sec >= 0) && (t.sec <= 59);
}

// Seconds since 1 March of year 0 in the proleptic Gregorian calendar (the
// days_from_civil method). 64 bits, because the count passes 2^31 in year 68.
static int64_t clock_seconds(int year, int month, int day, int hour, int min, int sec)
{
    year -= (month <= 2) ? 1 : 0;
    int32_t era = year / 400;
    int32_t yoe = year - (era * 400);
    int32_t doy = ((153 * (month + ((month > 2) ? -3 : 9)) + 2) / 5) + day - 1;
    int32_t doe = (yoe * 365) + (yoe / 4) - (yoe / 100) + doy;
    int32_t days = (era * 146097) + doe;
    return ((int64_t)days * 86400) + (((hour * 60) + min) * 60) + sec;
}

// The calendar moment of a count from clock_seconds(), the civil_from_days method, with
// the day of week derived from the date. Only years from 1 on reach it.
static void clock_from_seconds(int64_t seconds, clock_time_t& t)
{
    int32_t days = (int32_t)(seconds / 86400);
    int32_t rest = (int32_t)(seconds - ((int64_t)days * 86400));
    int32_t era = days / 146097;
    int32_t doe = days - (era * 146097);
    int32_t yoe = (doe - (doe / 1460) + (doe / 36524) - (doe / 146096)) / 365;
    int32_t doy = doe - ((365 * yoe) + (yoe / 4) - (yoe / 100));
    int32_t mp = ((5 * doy) + 2) / 153;
    t.day = doy - (((153 * mp) + 2) / 5) + 1;
    t.month = (mp < 10) ? (mp + 3) : (mp - 9);
    t.year = yoe + (era * 400) + ((t.month <= 2) ? 1 : 0);
    t.hour = rest / 3600;
    t.min = (rest / 60) % 60;
    t.sec = rest % 60;
    t.wd = clock_day_of_week(t.year, t.month, t.day);
}

// T-W in the four forms of SI-120, each laid out exactly as the matching T-R answers
// it. The A, B and D forms carry a day of week, which is checked for its range and not
// kept (SI-121): a read derives it from the date, as it does for the I form.
static int parse_clock_write(const uint8_t *buffer, int len, clock_time_t& t)
{
    const uint8_t *p = buffer + 4;
    int hour12, year2;

    switch (buffer[3]) {
    case 'A':
        // "dow. mo/da/yr hr:mi:se xM", with the marker and the space before it optional.
        if ((len != 26) && (len != 29)) {
            return ERR_SYNTAX;
        }
        for (t.wd = 0; t.wd < 7; t.wd++) {
            if (memcmp(p, c_weekday_4[t.wd], 2) == 0) {
                break;
            }
        }
        if ((t.wd == 7) || (p[4] != ' ') || (p[7] != '/') || (p[10] != '/') ||
            (p[13] != ' ') || (p[16] != ':') || (p[19] != ':')) {
            return ERR_SYNTAX;
        }
        t.month = clock_field(p + 5, 2);
        t.day   = clock_field(p + 8, 2);
        year2   = clock_field(p + 11, 2);
        t.hour  = clock_field(p + 14, 2);
        t.min   = clock_field(p + 17, 2);
        t.sec   = clock_field(p + 20, 2);
        if ((t.month < 0) || (t.day < 0) || (year2 < 0) ||
            (t.hour < 0) || (t.min < 0) || (t.sec < 0)) {
            return ERR_SYNTAX;
        }
        t.year = clock_year_of(year2);
        // Without the marker the hour is already a 24 hour time.
        if (len == 29) {
            if ((p[22] != ' ') || (p[24] != 'M') || ((p[23] != 'A') && (p[23] != 'P'))) {
                return ERR_SYNTAX;
            }
            if (t.hour == 12) {
                t.hour = 0;
            }
            if (p[23] == 'P') {
                t.hour += 12;
            }
        }
        break;

    case 'B':
    case 'D':
        // wd, year, month, day, hour in 12 hour form, minute, second, PM flag. The
        // fields are BCD for B and binary for D, and none of them is a terminator.
        if (len < 12) {
            return ERR_SYNTAX;
        }
        if (buffer[3] == 'B') {
            t.wd  = clock_bcd(p[0]);
            year2 = clock_bcd(p[1]);
            t.month = clock_bcd(p[2]);
            t.day   = clock_bcd(p[3]);
            hour12  = clock_bcd(p[4]);
            t.min   = clock_bcd(p[5]);
            t.sec   = clock_bcd(p[6]);
        } else {
            t.wd  = (int)p[0];
            year2 = (int)p[1];
            t.month = (int)p[2];
            t.day   = (int)p[3];
            hour12  = (int)p[4];
            t.min   = (int)p[5];
            t.sec   = (int)p[6];
        }
        if ((year2 < 0) || (hour12 < 0)) {
            return ERR_SYNTAX;
        }
        t.year = clock_year_of(year2);
        // A twelve in the hour field is midnight or noon, and the flag adds the half day.
        t.hour = (hour12 == 12) ? 0 : hour12;
        if (p[7]) {
            t.hour += 12;
        }
        break;

    case 'I':
        // "YYYY-MM-DDThh:mm:ss", with the day of week a T-RI answer ends in ignored.
        if ((len != 23) && ((len != 27) || (p[19] != ' '))) {
            return ERR_SYNTAX;
        }
        if ((p[4] != '-') || (p[7] != '-') || (p[10] != 'T') || (p[13] != ':') || (p[16] != ':')) {
            return ERR_SYNTAX;
        }
        t.year  = clock_field(p, 4);
        t.month = clock_field(p + 5, 2);
        t.day   = clock_field(p + 8, 2);
        t.hour  = clock_field(p + 11, 2);
        t.min   = clock_field(p + 14, 2);
        t.sec   = clock_field(p + 17, 2);
        if ((t.year < CLOCK_FIRST_YEAR) || (t.year > CLOCK_LAST_YEAR) ||
            (t.month < 1) || (t.month > 12)) {
            return ERR_SYNTAX;
        }
        t.wd = clock_day_of_week(t.year, t.month, t.day);
        break;

    default:
        return ERR_SYNTAX;
    }

    if (!clock_time_valid(t)) {
        return ERR_SYNTAX;
    }
    return 0;
}

int IecParser :: time_command(const uint8_t *buffer, int len)
{
    const char *const *wd4 = c_weekday_4;
    const char *const *wd3 = c_weekday_3;
    uint8_t result[32];

    if (buffer[1] != '-') {
        return ERR_SYNTAX;
    }

    int wd, day, month, year, hour, min, sec, hour12;

    // The drive's clock is the system clock plus the offset a T-W set (SI-120). Without
    // one the answer is the system clock as it reads, day of week included.
    get_current_time(wd, year, month, day, hour, min, sec);
    int64_t now = clock_seconds(year, month, day, hour, min, sec);
    int64_t offset = exec->get_clock_offset();
    if (offset) {
        clock_time_t t;
        clock_from_seconds(now + offset, t);
        wd = t.wd; year = t.year; month = t.month; day = t.day;
        hour = t.hour; min = t.min; sec = t.sec;
    }

    hour12 = (hour % 12); if (hour12 == 0) hour12 = 12;

    int reslen;
    switch(buffer[2]) {
    case 'R':
        if (len != 4) {
            return ERR_SYNTAX;
        }
        switch(buffer[3]) {
        case 'A': // ASCII
            // "dow. mo/da/yr hr:mi:se xx"+CHR$(13): month first, as in every other
            // date this drive prints.
            reslen = sprintf((char *)result, "%s %02d/%02d/%02d %02d:%02d:%02d %s\r",
                wd4[wd], month, day, year % 100, hour12, min, sec, (hour >= 12)?"PM":"AM");  
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
        return exec->do_cmd_response(result, reslen);
        break;
    case 'W': {
        // The clock the drive answers with is its own: the write sets how far it is from
        // the system clock, which it does not change (SI-120).
        clock_time_t t;
        if (len < 4) {
            return ERR_SYNTAX;
        }
        int err = parse_clock_write(buffer, len, t);
        if (err) {
            return err;
        }
        exec->set_clock_offset(clock_seconds(t.year, t.month, t.day, t.hour, t.min, t.sec) - now);
        return 0;
    }
    }
    return ERR_SYNTAX;
}

int IecParser :: user_command(const uint8_t *buffer, int len)
{
    int n;
    int p[4];
    const uint8_t *params = buffer + 2;
    int param_len = (len > 2) ? (len - 2) : 0;

    // CBM DOS selects the user command from the low nibble of the character after
    // the U, so U1 and UA are the same command, U2 and UB are the same, and so are
    // U9 and UI, and U: and UJ.
    switch(buffer[1]) {
    case '1':
    case 'A':
        n = parse_block_parameters(params, param_len, p, 4);
        if (n != 4) return ERR_SYNTAX;
        return exec->do_block_read(p[0], p[1], p[2], p[3], false);
    case '2':
    case 'B':
        n = parse_block_parameters(params, param_len, p, 4);
        if (n != 4) return ERR_SYNTAX;
        return exec->do_block_write(p[0], p[1], p[2], p[3], false);
    case '9':
    case 'I':
        // UI+ and UI- only select the serial bus timing. They are not a reset, so
        // they must not answer with the power-up message the way UI does.
        if ((param_len > 0) && ((params[0] == '+') || (params[0] == '-'))) {
            return 0;
        }
        return exec->do_initialize();
    case ':':
    case 'J':
        return exec->do_reset(false);
    case 0xCA: // U+shifted J
        return exec->do_reset(true);
    case '0':
        // U0>+CHR$(d) changes the device number (SI-100). CBM DOS takes the > from its
        // low five bits, as sd2iec does. The other U0 forms select serial timing and
        // retries this drive does not have.
        if ((len == 4) && ((params[0] & 0x1F) == 0x1E) && (params[1] >= 8) && (params[1] <= 30)) {
            return exec->do_set_device_number(params[1]);
        }
        return ERR_SYNTAX;
    default:
        return ERR_SYNTAX;
    }
    return 0;
}

// BASIC's PRINT# ends a command with a carriage return, and CBM DOS drops it before
// reading the command: the 1541 ROM does that at $C2B3. Change Partition in its
// binary form is the exception, because its partition byte is not optional, so a
// byte that happens to be a carriage return is that parameter. CMD DOS has the same
// ambiguity wherever the last parameter is optional, and its manual answers it by
// telling programmers to send the terminator themselves when they want partition 13.
//
// BASIC's PRINT# to a logical file number of 128 or more ends a line with a carriage
// return and a line feed (C64 ROM $AAD7), and both are dropped. The ROM also ends a
// command at any carriage return second to last (SI-016), but that would cut a binary
// parameter of 13 short, which the reporter of #877 asked not to reproduce.
static int strip_terminator(const uint8_t *buffer, int len)
{
    if ((len > 1) && (buffer[0] == 'C') && (buffer[1] == 0xD0)) {
        return len;
    }
    // U0> is not complete without its number byte, so a 13 there is device 13 (SI-100).
    if ((len == 4) && (memcmp(buffer, "U0>", 3) == 0)) {
        return len;
    }
    if (len && (buffer[len - 1] == 0x0D)) {
        return len - 1;
    }
    if ((len > 2) && (buffer[len - 2] == 0x0D) && (buffer[len - 1] == 0x0A)) {
        return len - 2;
    }
    return len;
}

int IecParser :: execute_command(const uint8_t *buffer, int len)
{
    if (len >= CBMDOS_COMMAND_BUFFER_SIZE) {
        return ERR_CMD_TOO_LONG; // SI-022: refused, not executed cut short
    }
    const int original_len = len;
    len = strip_terminator(buffer, len);
    if (len <= 0) { // a lone carriage return: the ROM at $C175 and sd2iec answer 31
        return ERR_UNKNOWN_CMD;
    }
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
    case 'G': return get_command(buffer, len);
    case 'I': return initialize_command(buffer, len);
    case 'M':
        if (buffer[1] == '-') {
            return memory_command(buffer, len);
        }
        return dir_command(buffer, len);
    case 'N': return format_command(buffer, len);
    case 'P': return position_command(buffer, original_len, len); // its last byte is data (SI-018)
    case 'R':
        if (buffer[1] == 'D') {
            return dir_command(buffer, len);
        }
        if ((len > 2) && (buffer[1] == '-')) {
            return rename_dashed_command(buffer, len);
        }
        return rename_command(buffer, len);
    case 'S':
        if ((len == 3) && (buffer[1] == '-')) {
            return swap_command(buffer, len);
        }
        return scratch_command(buffer, len);
    case 'T': return time_command(buffer, len);
    case 'W':
        // W-1 sets the software write protect and W-0 clears it (SI-102, HD 9-35).
        if ((len == 3) && (buffer[1] == '-') && ((buffer[2] == '0') || (buffer[2] == '1'))) {
            return exec->do_set_write_protect(buffer[2] == '1');
        }
        return ERR_SYNTAX;
    case 'U': return user_command(buffer, len);
    case 'A':
    case 'D':
    case 'E':
    case 'X':
        return attribute_command(buffer, len);
    case 'L':
        return lock_command(buffer, len);
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
        // 07/27/19   03.44 PM. The three spaces are what a CMD HD prints between the date
        // and the time in a long listing, measured on HD 9-22 and matching SD createentry().
        sprintf(buf, "%02d/%02d/%02d   %02d.%02d %cM", M, D, y % 100, h12, m, (h >= 12)?'P':'A');
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
