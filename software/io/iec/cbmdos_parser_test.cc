// Written by Gideon (2025)
// This module handles filename and command parsing for access to the VFS
// from the side of the commodore in a CMDHD / SD2IEC fashion

#include "cbmdos_parser.h"
#include "pattern.h"
#include "iec_log.h"

typedef struct {
    int partition;
    const char *path;
    const char *file;
    bool wildcard;
    bool replace;
    filetype_t type;
    fileaccess_t access;
    stream_type_t stream;
    stamp_format_t timefmt;
    uint32_t min_time;
    uint32_t max_time;
    uint8_t filetypes;
    uint8_t partition_types;
} open_result_t;

const open_result_t c_open_result_init = { 0, "", "", false, false, e_any, e_not_set,
                                            e_stream_file, e_stamp_none, 0x0, 0x0, 0x00, 0x00 };

#include "cbmdos_stubs.cc"
IecCommandExecuterStubs exec;
IecParser parser(&exec);

void print_file(filename_t& file)
{
    // printf("  Partition: %d\n", file.partition);
    // printf("  Path: '%s'\n", file.path.c_str());
    // printf("  Filename: '%s'\n", file.filename.c_str());
    // printf("  Wildcard: %s\n", file.has_wildcard ? "true" : "false" );
    printf("P=%d, Path='%s', Filename='%s', Wildcard: %s\n",
        file.partition, file.path.c_str(), file.filename.c_str(), file.has_wildcard?"true":"false" );
}

void print_open(open_t& o)
{
    char buf[24];
    print_file(o.file);

    const char *strm[] = { "File", "Buffer", "Directory of Files", "Directory of Partitions" };
    printf("  Replace: %s\n", o.replace ? "true" : "false");
    printf("  FileType: %d\n", (int)o.filetype);
    printf("  Access: %d\n", (int)o.access);
    printf("  Stream: %s\n", strm[(int)o.dir_opt.stream]);

    if ((o.dir_opt.stream == e_stream_dir) || (o.dir_opt.stream == e_stream_partitions)) {
        const char *fmt[] = { "None", "Short", "Long "};
        printf("  Stamp Format: %s\n", fmt[(int)o.dir_opt.timefmt] );
        printf("  File Types: %02x\n", o.dir_opt.filetypes);
        printf("  Partition Types: %02x\n", o.dir_opt.partition_types);
        if (o.dir_opt.min_datetime) {
            printf("  From Time: %s (%08x)\n", cbmdos_time(o.dir_opt.min_datetime, buf, true), o.dir_opt.min_datetime);
        }
        if (o.dir_opt.max_datetime) {
            printf("  To Time: %s (%08x)\n", cbmdos_time(o.dir_opt.max_datetime, buf, true), o.dir_opt.max_datetime);
        }
    }
}

int failures = 0;

void d_parse_open(const char *buf, open_t& o, int expected_retval = 0, open_result_t result = c_open_result_init)
{
    int err = parse_open(buf, o);

    bool ok = true;
    ok &= (err == expected_retval);
    if (!err) {
        ok &= (o.file.partition == result.partition);
        ok &= (o.file.path == result.path);
        ok &= (o.file.filename == result.file);
        ok &= (o.filetype == result.type);
        ok &= (o.access == result.access);
        ok &= (o.replace == result.replace);
        ok &= (o.file.has_wildcard == result.wildcard);
        ok &= (o.dir_opt.stream == result.stream);
        ok &= (o.dir_opt.timefmt == result.timefmt);
        ok &= (o.dir_opt.filetypes == result.filetypes);
        ok &= (o.dir_opt.partition_types == result.partition_types);
        ok &= (o.dir_opt.min_datetime == result.min_time);
        ok &= (o.dir_opt.max_datetime == result.max_time);
    }
    if (ok) {
        printf("Open '%s' => OK!\n", buf);
        return;
    }

    if (err) {
        printf("Open '%s' is invalid: %d, expected %d\n", buf, err, expected_retval);
    } else {
        printf("Open '%s' results in:\n", buf);
        print_open(o);
    }
    printf("\n");
    failures++;
}

void test_command(int exp_retval, const uint8_t *cmd, int len)
{
    int retval = parser.execute_command(cmd, len);
    if (retval != exp_retval) {
        printf("Command '%s' returned %d, expected %d\n", cmd, retval, exp_retval);
        failures++;
    } else {
        printf("Command '%s' => OK!\n", cmd);
    }
}

// Checks what the parser passed on, not only that it accepted the command. Pass
// NULL for the command name to require that nothing at all was dispatched.
void test_dispatch(const char *cmd, int len, int exp_retval,
                   const char *what, int a = 0, int b = 0, int c = 0, int d = 0)
{
    last_stub_call.command = NULL;
    int retval = parser.execute_command((const uint8_t *)cmd, len);
    bool ok = (retval == exp_retval);
    if (what) {
        ok = ok && last_stub_call.command && (strcmp(last_stub_call.command, what) == 0) &&
             (last_stub_call.a == a) && (last_stub_call.b == b) &&
             (last_stub_call.c == c) && (last_stub_call.d == d);
    } else {
        ok = ok && (last_stub_call.command == NULL);
    }
    if (ok) {
        printf("Dispatch '%s' => OK!\n", cmd);
        return;
    }
    printf("Dispatch '%s' returned %d (expected %d) and called %s(%d,%d,%d,%d), expected %s(%d,%d,%d,%d)\n",
           cmd, retval, exp_retval,
           last_stub_call.command ? last_stub_call.command : "nothing",
           last_stub_call.a, last_stub_call.b, last_stub_call.c, last_stub_call.d,
           what ? what : "nothing", a, b, c, d);
    failures++;
}

// The separators CBM DOS accepts between the parameters of a block command, and
// the aliases its user commands answer to. The 1541 ROM parses these parameters at
// $CC6F, skipping a run of space, comma or cursor right before each number, and
// skipping one colon between the command word and the first parameter. Its user
// command dispatch takes the low four bits of the character after the U, which is
// why U1 and UA are one command and U9 and UI are another.
void test_block_command_forms(void)
{
    // What PRINT#15,"U1:";2;0;18;0 puts on the bus: BASIC prints a space before and
    // after every positive number. This is the earlier VIEW BAM on the 1541
    // TEST/DEMO disk, and the reproduction in issue #876.
    test_dispatch("U1: 2  0  18  0 \r", 17, 0, "block read", 2, 0, 18, 0);
    // What the 9/84 revision of the same program sends.
    test_dispatch("U1:2,0,18,0\r", 12, 0, "block read", 2, 0, 18, 0);
    test_dispatch("B-P:2,144\r", 10, 0, "buffer position", 2, 144);
    // The remaining separator spellings.
    test_dispatch("U1 2,0,18,0", 11, 0, "block read", 2, 0, 18, 0);
    test_dispatch("U1:2 0 18 0", 11, 0, "block read", 2, 0, 18, 0);
    test_dispatch("U1 2\x1D""0\x1D""18\x1D""0", 11, 0, "block read", 2, 0, 18, 0);
    test_dispatch("U1:2, 0 ,18,\x1D""0", 14, 0, "block read", 2, 0, 18, 0);
    test_dispatch("B-R:2,0,18,1", 12, 0, "block read", 2, 0, 18, 1);
    test_dispatch("B-W: 2  0  18  2 ", 17, 0, "block write", 2, 0, 18, 2);
    // Allocate and free take three numbers, the partition, the track and the sector,
    // as both the 1541 and the CMD manuals write them. This is what CHECK DISK on
    // the 1541 TEST/DEMO disk sends, as PRINT#15,"B-A:"D$;T;S. A fourth number is
    // ignored, which is what the drive does with a parameter it never reads.
    test_dispatch("B-A:0,17,1", 10, 0, "block allocate", 0, 17, 1);
    test_dispatch("B-F:0,17,1", 10, 0, "block free", 0, 17, 1);
    test_dispatch("B-A: 0  17  1 ", 14, 0, "block allocate", 0, 17, 1);
    test_dispatch("B-A:2,0,16,3", 12, 0, "block allocate", 2, 0, 16);
    test_dispatch("B-A:0,17", 8, ERR_SYNTAX, NULL);
    test_dispatch("B-P: 2  0 ", 10, 0, "buffer position", 2, 0);

    // U1 and UA are the same command, and so are U2 and UB.
    test_dispatch("UA:2,0,18,0", 11, 0, "block read", 2, 0, 18, 0);
    test_dispatch("UB:2,0,18,5", 11, 0, "block write", 2, 0, 18, 5);

    // Parameters that are not there, and characters that are not separators.
    test_dispatch("U1:2,0", 6, ERR_SYNTAX, NULL);
    test_dispatch("B-R:", 4, ERR_SYNTAX, NULL);
    test_dispatch("B-R", 3, ERR_SYNTAX, NULL);
    test_dispatch("B-R:X,0,18,0", 12, ERR_SYNTAX, NULL);
    test_dispatch("B-P:2", 5, ERR_SYNTAX, NULL);

    // A multi digit parameter, and one long enough to overflow a smaller accumulator.
    test_dispatch("B-P:12,255", 10, 0, "buffer position", 12, 255);
    test_dispatch("B-P:2,99999999999", 17, 0, "buffer position", 2, 0xFFFF);

    // U9 and UI reset the drive, and so do U: and UJ. UI+ and UI- only select the
    // serial bus timing, so they must not reset anything.
    test_dispatch("UI", 2, 73, "initialize");
    test_dispatch("U9", 2, 73, "initialize");
    // UJ and U: close the data channels; U+shifted J also returns every partition to its
    // root (SI-103). Both answer with the DOS version.
    test_dispatch("UJ", 2, 73, "reset", 0);
    test_dispatch("U:", 2, 73, "reset", 0);
    test_dispatch("U\xCA", 2, 73, "reset", 1);
    test_dispatch("UJ\r", 3, 73, "reset", 0);
    test_dispatch("UI+", 3, 0, NULL);
    test_dispatch("UI-", 3, 0, NULL);
    test_dispatch("U9+", 3, 0, NULL);
    // U3 to U8 jump into a drive buffer, which has no equivalent here. U is a command
    // letter, so only the sub-command is unknown: 30, not 31 (SI-104).
    test_dispatch("U3:2,0,18,0", 11, 30, NULL);

    // The position command takes the secondary address either on its own or with 96
    // added to it, which is the form the manuals document. Three parameter bytes are
    // both a byte position and a record number with an offset; the drive decides
    // which of the two it uses from the state of the channel.
    test_dispatch("P\x02\x01\x00\x01", 5, 0, "set position", 2, 0x10001, 1, 1);
    test_dispatch("P\x62\x01\x00\x01", 5, 0, "set position", 0x62, 0x10001, 1, 1);
    // A record number without an offset, sent without a terminator, as a machine language
    // caller sends it: the 1541 ROM takes the record from both bytes.
    test_dispatch("P\x62\x05\x01", 4, 0, "set position", 0x62, 0x105, 0x105, 0);
    // The documented BASIC form with one position byte: the carriage return behind it is
    // not the next byte of the position (GSD "Positioning (seeking) Within a File"). A
    // relative file would read it as the record's high byte, as the ROM does.
    test_dispatch("P\x02\x64\r", 4, 0, "set position", 2, 100, 0xD64, 0);
}

// What happens to the carriage return BASIC's PRINT# appends, and to the commands
// whose last parameter can be that same byte. CBM DOS drops one carriage return
// from the end of the command before reading it; the 1541 ROM does that at $C2B3.
// Where the last parameter is optional this leaves an ambiguity, and the CMD hard
// disk manual answers it for G-P: "To avoid problems with reading information from
// Partition 13, the G-P command should always be sent with a trailing carriage
// return (CHR$(13))." Change Partition in its binary form is the one command whose
// last parameter is mandatory, so it keeps a parameter of 13.
void test_command_terminator(void)
{
    // A lone carriage return is not a command the drive knows: 31, as the ROM answers.
    test_dispatch("\r", 1, 31, NULL);

    // C<shift-P> selects a partition by a byte, so 13 is a partition number whether
    // or not a terminator followed it. This is the regression reported against the
    // first version of this fix.
    test_dispatch("C\xD0\x0D", 3, 0, "select partition", 13);
    test_dispatch("C\xD0\x0D\x0D", 4, 0, "select partition", 13);
    test_dispatch("C\xD0\x04", 3, 0, "select partition", 4);
    test_dispatch("C\xD0\x04\x0D", 4, 0, "select partition", 4);
    test_dispatch("C\xD0", 2, ERR_SYNTAX, NULL);
    // CPn takes the same number in ASCII, where no byte is ambiguous.
    test_dispatch("CP4", 3, 0, "select partition", 4);
    test_dispatch("CP13\r", 5, 0, "select partition", 13);
    // Partition 0 means the one already selected, which is also what no number asks
    // for.
    test_dispatch("CP\r", 3, 0, "select partition", 0);

    // G-P without a number, and with 255, both ask about the current partition, which
    // the parser passes on as -1. 0 asks about the system partition, a different
    // question (SI-041).
    test_dispatch("G-P", 3, 0, "partition info", -1);
    test_dispatch("G-P\r", 4, 0, "partition info", -1);
    test_dispatch("G-P\xFF", 4, 0, "partition info", -1);
    test_dispatch("G-P\x00", 4, 0, "partition info", 0);
    test_dispatch("G-P\x04", 4, 0, "partition info", 4);
    test_dispatch("G-P\x04\r", 5, 0, "partition info", 4);
    // Partition 13 needs the terminator the manual asks for, exactly as on a CMD
    // drive.
    test_dispatch("G-P\x0D\x0D", 5, 0, "partition info", 13);

    // The position command is exempt from the terminator rule, because its last
    // parameter byte is data (SI-018, SD parse_position() restores the length from
    // before the strip): an offset of 13 arrives with or without a terminator, and a
    // terminator behind the parameters is ignored.
    test_dispatch("P\x02\x0A\x00\x0D\x0D", 6, 0, "set position", 2, 0xD000A, 10, 13);
    test_dispatch("P\x02\x0A\x00\x0D", 5, 0, "set position", 2, 0xA, 10, 13);

    // A command whose parameters are text loses the terminator rather than reading
    // it as a value.
    test_dispatch("B-P:2,13\r", 9, 0, "buffer position", 2, 13);
}

// The error numbers of doc/softiec_compatibility_spec.md SI-030, one case per return
// site whose number differs from 30. The numbers are written out rather than taken
// from the constants, so a constant with the wrong value cannot pass.
//   30 the command was recognised and its arguments were not
//   31 the first character is not a command letter
//   33 a wildcard or a character a name cannot carry
//   34 no name, or a colon with nothing after it
// Like test_dispatch, and also compares the names the command carried, as the stub
// recorded them.
void test_dispatch_text(const char *cmd, int len, int exp_retval, const char *what,
                        const char *text, int a = 0)
{
    last_stub_call.command = NULL;
    last_stub_call.text[0] = 0;
    int retval = parser.execute_command((const uint8_t *)cmd, len);
    bool ok = (retval == exp_retval) && last_stub_call.command &&
              (strcmp(last_stub_call.command, what) == 0) &&
              (strcmp(last_stub_call.text, text) == 0) && (last_stub_call.a == a);
    if (ok) {
        printf("Dispatch '%s' => OK!\n", cmd);
        return;
    }
    printf("Dispatch '%s' returned %d (expected %d) and called %s(%d,'%s'), expected %s(%d,'%s')\n",
           cmd, retval, exp_retval,
           last_stub_call.command ? last_stub_call.command : "nothing",
           last_stub_call.a, last_stub_call.text, what, a, text);
    failures++;
}

// The commands phase 3 of the specification adds, and the routing that has to find
// them before it falls through to rename and scratch.
void test_added_commands(void)
{
    // S-8, S-9 and S-D swap device numbers, which this drive does not do; they are not a
    // scratch of a file called -8 (SD parse_doscommand()).
    test_dispatch("S-8", 3, 31, NULL);
    test_dispatch("S-D\r", 4, 31, NULL);
    // SI-100: U0> followed by the device number as a byte.
    test_dispatch("U0>\x0C", 4, 0, "device number", 12);
    test_dispatch("U0>\x1E\r", 5, 0, "device number", 30);
    test_dispatch("U0>\x0D\r", 5, 0, "device number", 13);
    test_dispatch("U0>\x07", 4, 30, NULL);
    test_dispatch("U0>\x1F", 4, 30, NULL);
    test_dispatch("U0", 2, 30, NULL);
    test_dispatch("U0+", 3, 30, NULL);

    // SI-105 and SI-112: M-R answers the number of bytes asked for, every one of them
    // zero; no count means one, a count of zero means 256, and it stops at the end of
    // the page. M-W and M-E are refused, because this drive runs no drive code.
    test_dispatch("M-R\xA4\xFE\x02", 6, 0, "command response", 2);
    test_dispatch("M-R\x02\x00\x02\r", 7, 0, "command response", 2);
    test_dispatch("M-R\xA4\xFE", 5, 0, "command response", 1);
    test_dispatch("M-R\x00\xFE\x00", 6, 0, "command response", 256);
    test_dispatch("M-R\xF0\x00\x20", 6, 0, "command response", 16);
    test_dispatch("M-R\xA4", 4, 30, NULL);
    test_dispatch("M-W\x00\x05\x01\xEA", 7, 30, NULL);
    test_dispatch("M-E\x00\x05", 5, 30, NULL);
    test_dispatch("M-X", 3, 30, NULL);
    // SI-120: the clock belongs to the system, so T-W answers 30 rather than an OK that
    // would set nothing.
    test_dispatch("T-WI2026-09-12T13:02:03", 23, 30, NULL);
    test_dispatch("MD:DIR", 6, 0, NULL); // still a directory command
}

// SI-021, SI-022 and SI-016: the command buffer holds 254 bytes, a command that fills
// it answers 32 and is not executed, and a carriage return second to last ends the
// command, as the 1541 ROM does at $C2B3.
void test_command_length_and_terminator(void)
{
    char cmd[300];
    memset(cmd, ' ', sizeof(cmd));
    memcpy(cmd, "B-P:2,144", 9);
    test_dispatch(cmd, 253, 0, "buffer position", 2, 144);
    test_dispatch(cmd, 254, 32, NULL);
    test_dispatch(cmd, 255, 32, NULL);

    test_dispatch("B-P:2,144\r\n", 11, 0, "buffer position", 2, 144);
    // Only a carriage return and a line feed end a command early: a 13 second to last is a
    // binary parameter, here the high byte of an M-R address, and the count behind it
    // stays.
    test_dispatch("M-R\x00\x0D\x05", 6, 0, "command response", 5);
}

// SI-060 and SI-063: MD requires a colon and refuses a name that is a shifted space;
// RD takes a name behind a colon and no path.
void test_md_rd_grammar(void)
{
    test_command(34, (const uint8_t *)"MD NOCOLON", 10);
    test_command(34, (const uint8_t *)"MD:\xA0", 4);
    test_command( 0, (const uint8_t *)"MD//SUB/:NAME", 13);
    test_command(34, (const uint8_t *)"RD/PROBEDIR", 11);
    test_command(34, (const uint8_t *)"RD//SUB/:NAME", 13);
    test_command(34, (const uint8_t *)"RDNAME", 6);
    test_command( 0, (const uint8_t *)"RD:NAME", 7);
    test_command( 0, (const uint8_t *)"RD12:NAME\r", 10);
}

// SI-147 and SI-142: the PETSCII to host name mapping shared with sd2iec's extension mode
// 5. A trailing run of shifted spaces is padding and is dropped; a shifted space inside a
// name is escaped; * and ? are escaped; and the braces a type extension gets stay inside the
// buffer. This suite is
// also built with -funsigned-char (target/pc/linux/parse_unsigned), because the shifted
// space rule depended on the signedness of char.
static void check_fat_name(const char *what, const char *pet, int maxlen, const char *expected)
{
    char fat[64];
    memset(fat, 'X', sizeof(fat));
    fat[sizeof(fat) - 1] = 0;
    petscii_to_fat(pet, fat, maxlen);
    if (strcmp(fat, expected) == 0) {
        printf("Name %s => OK!\n", what);
        return;
    }
    printf("Name %s mapped to '%s', expected '%s' (char is %s)\n", what, fat, expected,
           ((char)0xA0 < 0) ? "signed" : "unsigned");
    failures++;
}

void test_name_mapping(void)
{
    // Each name is followed by a byte that is not zero, so a test that reads past the
    // terminator sees something other than the end of the string.
    const char trailing[] = { 'A', 'B', (char)0xA0, 0, 'Z' };
    check_fat_name("AB+$A0", trailing, 51, "AB");
    const char trailing_run[] = { 'A', (char)0xA0, (char)0xA0, 0, 'Z' };
    check_fat_name("A+$A0$A0", trailing_run, 51, "A");
    const char interior[] = { 'A', (char)0xA0, 'B', 0, 'Z' };
    check_fat_name("A+$A0+B", interior, 51, "A{A0}B");
    const char interior_run[] = { 'A', (char)0xA0, (char)0xA0, 'B', 0, 'Z' };
    check_fat_name("A+$A0$A0+B", interior_run, 51, "A{A0A0}B");
    check_fat_name("star", "FOO*", 51, "FOO{2A}");
    check_fat_name("question mark", "WHAT?", 51, "WHAT{3F}");
    // A name that fills the buffer and ends in .PRG gets no braces, which would not fit:
    // with a 48 byte buffer, 46 characters and the terminator are all there is room for.
    check_fat_name("braces bound", "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOP.PRG", 48,
                   "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOP.PRG");
    check_fat_name("braces", "GAME.PRG", 48, "GAME.PRG{}");
}

// SI-076: L toggles the lock of one entry.
void test_lock_command(void)
{
    test_dispatch_text("L:TEST", 6, 0, "lock", "-1||TEST");
    test_dispatch_text("L1//:TEST\r", 10, 0, "lock", "1|//|TEST");
    test_dispatch("L:", 2, 34, NULL);
}

// B-P positions within the 256 byte buffer; a third number is ignored, as the ROM ignores it.
// SI-094: B-R and B-W use the first byte of the block as a length, U1 and U2 do not.
void test_block_positions_and_lengths(void)
{
    test_dispatch("B-P 9 4 1", 9, 0, "buffer position", 9, 4);
    test_dispatch("B-P:2,144", 9, 0, "buffer position", 2, 144);
    test_dispatch("B-R:2,0,18,1", 12, 0, "block read", 2, 0, 18, 1);
    test_dispatch("U1:2,0,18,1", 11, 0, "block read", 2, 0, 18, 1);
    if (!last_stub_call.text[0] || strcmp(last_stub_call.text, "no length")) {
        printf("U1 was not dispatched as a read without the length byte: '%s'\n", last_stub_call.text);
        failures++;
    }
    test_dispatch("B-R:2,0,18,1", 12, 0, "block read", 2, 0, 18, 1);
    if (strcmp(last_stub_call.text, "length")) {
        printf("B-R was not dispatched as a read with the length byte: '%s'\n", last_stub_call.text);
        failures++;
    }
    test_dispatch("B-W:2,0,18,1", 12, 0, "block write", 2, 0, 18, 1);
    if (strcmp(last_stub_call.text, "length")) {
        printf("B-W was not dispatched as a write with the length byte: '%s'\n", last_stub_call.text);
        failures++;
    }
}

void test_error_codes(void)
{
    // SI-031: not a command letter. CHR$(0) and A are what the reporter measured on
    // #877; Z and Q never become commands.
    test_dispatch("\0", 1, 31, NULL);
    test_dispatch("A", 1, 31, NULL);
    test_dispatch("Z", 1, 31, NULL);
    test_dispatch("Q\r", 2, 31, NULL);
    // A colon with nothing after it.
    test_dispatch("C99:EMPTY=", 10, 34, NULL);
    test_dispatch("R:NEW=", 6, 34, NULL);
    // A wildcard in the target of a copy or a rename.
    test_dispatch("C:NEW*=OLD", 10, 33, NULL);
    test_dispatch("R:NEW?=OLD", 10, 33, NULL);
    // A command letter followed by a sub-command that does not exist.
    test_dispatch("B-X:1,2,3", 9, 30, NULL);
    test_dispatch("B-E:2,0,18,0", 12, 30, NULL); // SI-095: no drive memory to run code in
    test_dispatch("MX", 2, 30, NULL);
    test_dispatch("G-X", 3, 30, NULL);
    test_dispatch("UZ", 2, 30, NULL);
    test_dispatch("T-X", 3, 30, NULL);
    test_dispatch("XFOO", 4, 30, NULL);
    test_dispatch("EFOO", 4, 30, NULL);

    // SI-053: I is a command of its own. It releases the channels a program left open and
    // answers OK; UI is the reset that answers with the DOS version.
    test_dispatch("I", 1, 0, "initialize buffers");
    test_dispatch("I0:\r", 4, 0, "initialize buffers");
    test_dispatch("UI", 2, 73, "initialize");

    // An open name carrying a character a name cannot carry.
    open_t o;
    d_parse_open("FILE=X", o, 33);
    d_parse_open("@345:", o, 34);
}

// The byte formatter of the Software IEC failure log. It takes a length and
// must never read the payload as a string, because an IEC payload can carry an
// embedded zero, a carriage return that is a parameter rather than a terminator, and
// shifted PETSCII bytes above 0x7F.
void test_log_render(const char *what, const uint8_t *data, int len, const char *exp_text)
{
    char txt[SOFTIEC_LOG_TEXT_SIZE];
    int txt_len = softiec_log_text(data, len, txt, sizeof(txt));
    if ((strcmp(txt, exp_text) == 0) && (txt_len == (int)strlen(exp_text))) {
        printf("Log %s => OK!\n", what);
        return;
    }
    printf("Log %s rendered \"%s\", expected \"%s\"\n", what, txt, exp_text);
    failures++;
}

// A rendering that does not fit has to say so rather than look like a short payload.
void test_log_truncation(void)
{
    const uint8_t data[] = { 0x41, 0x42, 0x43, 0x44 };
    char txt[6];
    softiec_log_text(data, 4, txt, sizeof(txt));
    if (strcmp(txt, "ABC..") == 0) {
        printf("Log truncation => OK!\n");
        return;
    }
    printf("Log truncation rendered \"%s\", expected \"ABC..\"\n", txt);
    failures++;
}

void test_log_formatters(void)
{
    const uint8_t empty[1] = { 0 };
    test_log_render("empty payload", empty, 0, "");

    // The command that started #881: C, shifted P, and partition 13 as a byte.
    const uint8_t change_partition[] = { 'C', 0xD0, 0x0D };
    test_log_render("binary change partition", change_partition, 3, "C\\xD0\\r");

    // A text command with the carriage return PRINT# appends behind it.
    const uint8_t text_command[] = { 'G', '-', 'P', 0x0D };
    test_log_render("text command with terminator", text_command, 4, "G-P\\r");

    // An embedded zero is a payload byte here, not the end of the payload.
    const uint8_t with_zero[] = { 'A', 0x00, 'B' };
    test_log_render("embedded zero", with_zero, 3, "A\\0B");

    // Quotes and backslashes have to survive the quoted rendering.
    const uint8_t quoting[] = { '"', '\\', 0x0A };
    test_log_render("quoting", quoting, 3, "\\\"\\\\\\n");

    test_log_truncation();
}

// The recursive pattern_match() the iterative one replaced, as the reference its results
// are compared with, with the one defect corrected: at the end of the fixed string only
// stars may be left of the pattern, where the recursion accepted anything after a star.
static bool reference_pattern_match(const char *p, const char *f, bool case_sensitive)
{
    do {
        if (!(*f)) {
            while (*p == '*') {
                p++;
            }
            return (*p == '\0');
        }
        if (!(*p)) {
            return false;
        }
        if (*p == '*') {
            p++;
            if (!(*p)) {
                return true;
            }
            while (*f) {
                if (reference_pattern_match(p, f, case_sensitive)) {
                    return true;
                }
                f++;
            }
            return false;
        } else if (*p != '?') {
            char cp = case_sensitive ? *p : toupper(*p);
            char cf = case_sensitive ? *f : toupper(*f);
            if (cf != cp) {
                return false;
            }
        }
        f++;
        p++;
    } while (true);
}

static int build_string(char *out, const char *alphabet, int base, int length, int index)
{
    for (int i = 0; i < length; i++) {
        out[i] = alphabet[index % base];
        index /= base;
    }
    out[length] = 0;
    return length;
}

// Every pattern of up to five characters from A, b, * and ? against every name of up to
// five characters from a and B, both case modes, gives the reference's answer; and a
// pattern of forty stars that does not match returns at once, where the recursion took
// time exponential in the number of stars.
void test_pattern_match(void)
{
    char pattern[8], fixed[8];
    int compared = 0;
    for (int plen = 0; plen <= 5; plen++) {
        int pcount = 1;
        for (int i = 0; i < plen; i++) pcount *= 4;
        for (int pi = 0; pi < pcount; pi++) {
            build_string(pattern, "Ab*?", 4, plen, pi);
            for (int flen = 0; flen <= 5; flen++) {
                for (int fi = 0; fi < (1 << flen); fi++) {
                    build_string(fixed, "aB", 2, flen, fi);
                    for (int cs = 0; cs < 2; cs++) {
                        bool want = reference_pattern_match(pattern, fixed, cs);
                        if (pattern_match(pattern, fixed, cs) != want) {
                            printf("pattern_match('%s', '%s', %d) differs from the reference, which says %d\n",
                                   pattern, fixed, cs, want);
                            failures++;
                            return;
                        }
                        compared++;
                    }
                }
            }
        }
    }
    // The defect the recursion had: a name that ends where the pattern has a star.
    if (pattern_match("FOO*X", "FOO", false) || !pattern_match("FOO*", "FOO", false) ||
        pattern_match("*.D64", "", false) || !pattern_match("*", "", false)) {
        printf("FOO*X matched FOO, FOO* did not match FOO, *.D64 matched nothing or * did not\n");
        failures++;
        return;
    }
    char stars[64];
    memset(stars, '*', 40);
    strcpy(stars + 40, "Q");
    if (pattern_match(stars, "ABCDEFGHIJKLMNOP", false) || !pattern_match(stars, "ABCDEFGHIJKLMNOQ", false)) {
        printf("forty stars and Q matched wrongly\n");
        failures++;
        return;
    }
    printf("Pattern match: %d comparisons with the reference, forty stars => OK!\n", compared);
}

int main(int argc, const char *argv[])
{
    test_log_formatters();
    test_pattern_match();

    open_t o;
    d_parse_open("JUSTFILE", o,     0,
                { -1, "", "JUSTFILE", false, false, e_any, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00} );

    d_parse_open("JUSTFILE,U", o,   0,
                { -1, "", "JUSTFILE", false, false, e_usr, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00} );

    d_parse_open("JUSTFILE,USR", o, 0,
                { -1, "", "JUSTFILE", false, false, e_usr, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00} );

    d_parse_open("JUSTFILE,U,A", o, 0,
                { -1, "", "JUSTFILE", false, false, e_usr, e_append,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00 } );

    d_parse_open("JUSTFILE,H", o,  ERR_SYNTAX );

    d_parse_open("0:FILEON0", o,    0,
                {  0, "", "FILEON0",  false, false, e_any, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00 } );

    d_parse_open("1:FILE/ON1", o,   0,
                {  1, "", "FILE/ON1", false, false, e_any, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00 } );

    d_parse_open("-2:FILE ON NEGATIVE", o, ERR_SYNTAX );

    d_parse_open("56:FILE ON 56", o, 0,
                { 56, "", "FILE ON 56", false, false, e_any, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00 } );

    d_parse_open("@:REPLACE", o,     0,
                { -1, "", "REPLACE",  false, true, e_any, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00 } );

    d_parse_open("@1:REPLACE ON 1", o, 0,
                {  1, "", "REPLACE ON 1", false, true, e_any, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00 } );

    d_parse_open("@1/HELLO:REPLACE ON 1 IN PATH HELLO", o, 0,
                { 1, "/HELLO", "REPLACE ON 1 IN PATH HELLO", false, true, e_any, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00 });

    d_parse_open("@78/HELLO/DEEPER:EVERYTHING,U,A", o, 0,
                { 78, "/HELLO/DEEPER", "EVERYTHING", false, true, e_usr, e_append,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00 });

    d_parse_open("//FROMROOT/DEEPER:BLAH,S,R", o, 0,
                { -1, "//FROMROOT/DEEPER", "BLAH", false, false, e_seq, e_read,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00});

    d_parse_open("@345:", o, 34); // no name after the colon

    d_parse_open(",", o, ERR_SYNTAX);

    d_parse_open("BLAH,SEQ", o, 0,
                {-1, "", "BLAH", false, false, e_seq, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00});

    d_parse_open("@1:FOO,S", o, 0,
                { 1, "", "FOO", false, true, e_seq, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00});

    d_parse_open("@1/HELLO?:DIR WITH WILDCARD", o, 0,
                { 1, "/HELLO?", "DIR WITH WILDCARD", false, true, e_any, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00});

    d_parse_open("@1/HELLO:FILE WITH WILD*", o, 0,
                { 1, "/HELLO", "FILE WITH WILD*", true, true, e_any, e_not_set,
                  e_stream_file, e_stamp_none, 0x0, 0x0, 0x00 });

    d_parse_open("$", o, 0,
                {-1, "", "", false, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_none, 0x0, 0x0, 0x00 });

    d_parse_open("$=T", o, 0,
                {-1, "", "", false, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_short, 0x0, 0x0, 0x00 });

    d_parse_open("$=P", o, 0,
                {-1, "", "", false, false, e_any, e_not_set,
                  e_stream_partitions, e_stamp_none, 0x0, 0x0, 0x00 });

    d_parse_open("$=T2", o, 0,
                { 2, "", "", false, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_short, 0x0, 0x0, 0x00 });

    d_parse_open("$=T2:*=P", o, 0,
                { 2, "", "*", true, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_short, 0x0, 0x0, 0x02 });

    d_parse_open("$=T2:*=P,L", o, 0,
                { 2, "", "*", true, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_long, 0x0, 0x0, 0x02 });

    d_parse_open("$=T2:*=P,L,>12/21/18 04:15 PM", o, 0,
                { 2, "", "*", true, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_long, 0x4D9581E0, 0x0, 0x02 });
                  // 2018=>38 => 0100110
                  // 12       => 1100
                  // 21       => 10101
                  // 16       => 10000
                  // 15       => 001111
                  // 00       => 00000
                  // 0100.1101.1001.0101.1000.0001.1110.0000  0x4D9581E0

    d_parse_open("$=T:*=L,<12/21/18 04:15 PM", o, 0, 
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_long, 0x0, 0x4D9581E0, 0x00 });

    d_parse_open("$=T4:*=S,N,>12/21/18 12:01 AM,<12/31/18 12:00 PM", o, 0, 
                { 4, "", "*", true, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_none, 0x4d950020, 0x4d9f6000, 0x04 });

    d_parse_open("$3", o, 0,
                { 3, "", "", false, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_none, 0x0, 0x00, 0x00 });

    d_parse_open("#", o, 0,
                { -1, "", "", false, false, e_any, e_not_set,
                  e_stream_buffer, e_stamp_none, 0x0, 0x00, 0x00 });

    d_parse_open("#2", o, 0,
                { 2, "", "", false, false, e_any, e_not_set,
                  e_stream_buffer, e_stamp_none, 0x0, 0x00, 0x00 });

    d_parse_open("$=P:S*", o, 0,
                { -1, "", "S*", true, false, e_any, e_not_set,
                  e_stream_partitions, e_stamp_none, 0x0, 0x00, 0x00 });

    d_parse_open("$=P:?A*", o, 0,
                { -1, "", "?A*", true, false, e_any, e_not_set,
                  e_stream_partitions, e_stamp_none, 0x0, 0x00, 0x00 });

    d_parse_open("$=P69", o, 0,
                { 69, "", "", false, false, e_any, e_not_set,
                  e_stream_partitions, e_stamp_none, 0x0, 0x00, 0x00 });

    // The partition directory filters by partition type, where a directory of files
    // takes its own options. LOAD"$=P:*=tp" with tp one of N, 4, 7, 8 or C.
    d_parse_open("$=P:*=N", o, 0,
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_partitions, e_stamp_none, 0x0, 0x00, 0x00, 0x02 });

    d_parse_open("$=P:*=4", o, 0,
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_partitions, e_stamp_none, 0x0, 0x00, 0x00, 0x04 });

    d_parse_open("$=P:*=7", o, 0,
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_partitions, e_stamp_none, 0x0, 0x00, 0x00, 0x08 });

    d_parse_open("$=P:*=8", o, 0,
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_partitions, e_stamp_none, 0x0, 0x00, 0x00, 0x10 });

    d_parse_open("$=P:*=C", o, 0,
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_partitions, e_stamp_none, 0x0, 0x00, 0x00, 0x20 });

    d_parse_open("$=P:*=4,8", o, 0,
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_partitions, e_stamp_none, 0x0, 0x00, 0x00, 0x14 });

    d_parse_open("$=P:*=X", o, ERR_SYNTAX);

    // SI-134: H is not a file type but asks for hidden files, which are listed anyway, so
    // it must not set a type bit: $:*=H lists what $:* lists.
    d_parse_open("$:*=H", o, 0,
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_none, 0x0, 0x00, 0x00 });
    d_parse_open("$:*=P,H", o, 0,
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_none, 0x0, 0x00, 0x02 });

    d_parse_open("$//", o, 0,
                { -1, "//", "", false, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_none, 0x0, 0x00, 0x00 });
    
    // A directory time stamp filter, which the firmware's own sscanf could not read.
    d_parse_open("$:*=>01/02/25 03:04 PM", o, 0,
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_none, make_fat_time(2025, 1, 2, 15, 4, 0), 0x00, 0x00 });
    d_parse_open("$:*=<12/31/79 11:59 AM", o, 0,
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_none, 0x0, make_fat_time(2079, 12, 31, 11, 59, 0), 0x00 });
    d_parse_open("$:*=>01/02/25 12:00 AM", o, 0,
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_none, make_fat_time(2025, 1, 2, 0, 0, 0), 0x00, 0x00 });
    d_parse_open("$:*=>01/02/25 12:00 PM", o, 0,
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_stream_dir, e_stamp_none, make_fat_time(2025, 1, 2, 12, 0, 0), 0x00, 0x00 });
    d_parse_open("$:*=>01/02/25 03:04", o, ERR_SYNTAX);
    d_parse_open("$:*=>01-02-25 03:04 PM", o, ERR_SYNTAX);
    d_parse_open("$:*=>january", o, ERR_SYNTAX);

    test_command( 0, (const uint8_t *)"C:S=/C64 OS/:S", 14);
    test_command( 0, (const uint8_t *)"B-R 2 0 18 1\r", 13);
    test_command( 0, (const uint8_t *)"B-W 2 0 18 2\r", 13);
    test_command( 0, (const uint8_t *)"B-A 2 0 16 3\r", 13);
    test_command( 0, (const uint8_t *)"B-F 2 0 16 4\r", 13);
    test_command( 0, (const uint8_t *)"U1 2 0 18 3\r", 12);
    test_command( 0, (const uint8_t *)"U2 2 0 18 4\r", 12);
    test_command( 0, (const uint8_t *)"B-P 2 234\r", 10);
    test_block_command_forms();
    test_command_terminator();
    test_error_codes();
    test_added_commands();
    test_command_length_and_terminator();
    test_md_rd_grammar();
    test_name_mapping();
    test_lock_command();
    test_block_positions_and_lengths();
    test_command(34, (const uint8_t *)"C99:EMPTY=", 10);
    test_command( 0, (const uint8_t *)"C1:FCOPY=3:FCOPY", 16);
    test_command( 0, (const uint8_t *)"C:FULLSTATS=STAT1,3:STAT3", 25);
    test_command( 0, (const uint8_t *)"C2:MCOPY=1/COPIERS/:MCOPY", 25);
    test_command( 0, (const uint8_t *)"COPY2/SUBDIR:COMBINED=1/COPIERS/:FILE1,1/COPIERS/:FILE2,2/OTHERDIR:FILE*", 73);
    test_command( 0, (const uint8_t *)"R1:BOOT1=BOOT", 13);
    test_command( 0, (const uint8_t *)"R1/UTILS/:NEWT=1/UTILS/:WW", 26);
    test_command( 0, (const uint8_t *)"S1:JUNK,3:C?*.BAS", 17);
    test_command( 0, (const uint8_t *)"S1/UTILS/:CO*", 13);
    test_command( 0, (const uint8_t *)"SCRATCH1/UTILS/:CO*", 19);
    test_command( 0, (const uint8_t *)"P\x02\x64", 3);
    test_command( 0, (const uint8_t *)"P\x02\xC8\0", 4);
    test_command( 0, (const uint8_t *)"P\x02\x2C\x01\0", 5);
    test_command( 0, (const uint8_t *)"P\x02\x90\x01\0\0", 6);
    // A position is at most four bytes wide; what follows them, which from BASIC is the
    // terminator, is ignored (SI-018).
    test_dispatch("P\x02\xF4\x01\0\0\r", 7, 0, "set position", 2, 0x1F4, 500, 0);
    test_command( 0, (const uint8_t *)"CD:TEMP", 7);
    test_command( 0, (const uint8_t *)"CD1//TEMP", 9);
    test_command( 0, (const uint8_t *)"CD1//TEMP/TEMP2", 15);
    test_command( 0, (const uint8_t *)"CD1_", 4);
    test_command(34, (const uint8_t *)"RD33_/BLAH", 10); // RD takes no path (SI-063)
    test_command( 0, (const uint8_t *)"CD/TEMP/TEMP2", 13);
    test_command( 0, (const uint8_t *)"T-RA", 4);
    test_command( 0, (const uint8_t *)"T-RI", 4);
    test_command( 0, (const uint8_t *)"T-RD", 4);
    test_command( 0, (const uint8_t *)"T-RB", 4);
    test_command( 0, (const uint8_t *)"CP", 2);
    test_command( 0, (const uint8_t *)"CP1", 3);
    test_command( 0, (const uint8_t *)"CP12", 4);
    test_command( 0, (const uint8_t *)"CP123", 5);
    test_command( 0, (const uint8_t *)"CP1234", 6);
    test_command( 0, (const uint8_t *)"C\xD0\x1F", 3);
    // N[n][path]:name[,id] (SI-071): the parser passes the name and the id on.
    test_dispatch_text("N3//DISKS/:HELLO.D64,AB", 23, 0, "format", "3|//DISKS/|HELLO.D64|AB");
    test_dispatch_text("N:HELLO,123\r", 12, 0, "format", "-1||HELLO|123");
    test_dispatch_text("N:HELLO", 7, 0, "format", "-1||HELLO|");
    test_dispatch("N:", 2, 34, NULL);
    test_dispatch("N:,AB", 5, 34, NULL);
    test_dispatch("NHELLO,AB", 9, 34, NULL);
    test_command( 0, (const uint8_t *)"MD:TEMP", 7);
    test_command( 0, (const uint8_t *)"MD1:TEMP", 8);
    test_command( 0, (const uint8_t *)"MD1//:TEMP", 10);
    test_command( 0, (const uint8_t *)"MD1//TEMP/:TEMP2", 16);
    test_command( 0, (const uint8_t *)"MD:", 3);
    test_command(34, (const uint8_t *)"MD", 2);                // MD needs a colon (SI-060)
    test_command(34, (const uint8_t *)"MD/PATH\xC1\xC2", 9);
    test_command( 0, (const uint8_t *)"MD:PATH\xC1\xC2", 9);
    test_command( 0, (const uint8_t *)"XPWD", 4);

    if (failures) {
        printf("\n%d command parsing check(s) failed.\n", failures);
        return 1;
    }
    printf("\nAll command parsing checks passed.\n");
    return 0;
}
