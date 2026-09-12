// Written by Gideon (2025)
// This module handles filename and command parsing for access to the VFS
// from the side of the commodore in a CMDHD / SD2IEC fashion

#include "cbmdos_parser.h"
#include "pattern.h"
#include "iec_trace.h"

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
    test_dispatch("UJ", 2, 73, "initialize");
    test_dispatch("U:", 2, 73, "initialize");
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
    // An empty command line is not a command.
    test_dispatch("\r", 1, 0, NULL);

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

    // G-P without a number, and with 255, both ask about the current partition. A
    // CMD drive answers 0 with its system partition; there is none here, so that
    // also reads back as the current one.
    test_dispatch("G-P", 3, 0, "partition info", 0);
    test_dispatch("G-P\r", 4, 0, "partition info", 0);
    test_dispatch("G-P\xFF", 4, 0, "partition info", 0);
    test_dispatch("G-P\x04", 4, 0, "partition info", 4);
    test_dispatch("G-P\x04\r", 5, 0, "partition info", 4);
    // Partition 13 needs the terminator the manual asks for, exactly as on a CMD
    // drive.
    test_dispatch("G-P\x0D\x0D", 5, 0, "partition info", 13);

    // The position command has the same optional last parameter. Sent from BASIC the
    // terminator is there and the offset of 13 arrives; without it the offset is
    // taken as the terminator and the command positions to a record instead.
    test_dispatch("P\x02\x0A\x00\x0D\x0D", 6, 0, "set position", 2, 0xD000A, 10, 13);
    test_dispatch("P\x02\x0A\x00\x0D", 5, 0, "set position", 2, 10, 0, 0);

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
    // SI-051: R-P renames a partition. It must not reach the file rename.
    test_dispatch_text("R-P:WORK=NATIVE 1", 17, 0, "rename partition", "WORK|NATIVE 1");
    test_dispatch_text("R-P:WORK=NATIVE 1\r", 18, 0, "rename partition", "WORK|NATIVE 1");
    test_dispatch("R-P:WORK", 8, 30, NULL);
    test_dispatch("R-P:=OLD", 8, 34, NULL);
    test_dispatch("R-P:NEW=", 8, 34, NULL);
    test_dispatch("R-X:FOO", 7, 30, NULL);
    // SI-064: R-H renames the header of a directory, with a partition and a path.
    test_dispatch_text("R-H:WORK", 8, 0, "rename header", "-1||WORK");
    test_dispatch_text("R-H3:DOWNLOADS\r", 15, 0, "rename header", "3||DOWNLOADS");
    test_dispatch_text("R-H1//ASSEM/:BUDDY64", 20, 0, "rename header", "1|//ASSEM/|BUDDY64");
    test_dispatch("R-H:NAME*", 9, 33, NULL);
    test_dispatch("R-H:ABCDEFGHIJKLMNOPQ", 21, 34, NULL); // 17 characters

    // SI-101: S-8, S-9 and S-D swap the device number; they are not a scratch of a
    // file called -8. Zero asks for the configured number.
    test_dispatch("S-8", 3, 0, "device number", 8);
    test_dispatch("S-9\r", 4, 0, "device number", 9);
    test_dispatch("S-D", 3, 0, "device number", 0);
    test_dispatch("S-X", 3, 30, NULL);
    // SI-100: U0> followed by the device number as a byte.
    test_dispatch("U0>\x0C", 4, 0, "device number", 12);
    test_dispatch("U0>\x1E\r", 5, 0, "device number", 30);
    test_dispatch("U0>\x0D\r", 5, 0, "device number", 13);
    test_dispatch("U0>\x07", 4, 30, NULL);
    test_dispatch("U0>\x1F", 4, 30, NULL);
    test_dispatch("U0", 2, 30, NULL);
    test_dispatch("U0+", 3, 30, NULL);

    // SI-102: W-1 and W-0 set and clear the write protect.
    test_dispatch("W-1", 3, 0, "write protect", 1);
    test_dispatch("W-0\r", 4, 0, "write protect", 0);
    test_dispatch("W-2", 3, 30, NULL);
    test_dispatch("W", 1, 30, NULL);

    // SI-105 and SI-112: M-R answers the number of bytes asked for, every one of them
    // zero; no count means one, a count of zero means 256, and it stops at the end of
    // the page. M-W and M-E are accepted and do nothing.
    test_dispatch("M-R\xA4\xFE\x02", 6, 0, "command response", 2);
    test_dispatch("M-R\x02\x00\x02\r", 7, 0, "command response", 2);
    test_dispatch("M-R\xA4\xFE", 5, 0, "command response", 1);
    test_dispatch("M-R\x00\xFE\x00", 6, 0, "command response", 256);
    test_dispatch("M-R\xF0\x00\x20", 6, 0, "command response", 16);
    test_dispatch("M-R\xA4", 4, 30, NULL);
    test_dispatch("M-W\x00\x05\x01\xEA", 7, 0, NULL);
    test_dispatch("M-E\x00\x05", 5, 0, NULL);
    test_dispatch("M-X", 3, 30, NULL);
    test_dispatch("MD:DIR", 6, 0, NULL); // still a directory command
}

void test_error_codes(void)
{
    // SI-031: not a command letter. CHR$(0) and A are what the reporter measured on
    // #877; Z and Q never become commands.
    test_dispatch("\0", 1, 31, NULL);
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

    // SI-053 and SI-054: I and V are commands of their own. I releases the channels a
    // program left open and answers OK; UI is the reset that answers with the DOS
    // version; V has nothing to validate and answers OK.
    test_dispatch("I", 1, 0, "initialize buffers");
    test_dispatch("I0:\r", 4, 0, "initialize buffers");
    test_dispatch("UI", 2, 73, "initialize");
    test_dispatch("V", 1, 0, NULL);
    test_dispatch("V0:\r", 4, 0, NULL);

    // An open name carrying a character a name cannot carry.
    open_t o;
    d_parse_open("FILE=X", o, 33);
    d_parse_open("@345:", o, 34);
}

/* ==== SOFTIEC-TRACE diagnostics for #877; removed together with them ==== */

// The byte formatters the #877 Software IEC diagnostics use. They take a length and
// must never read the payload as a string, because an IEC payload can carry an
// embedded zero, a carriage return that is a parameter rather than a terminator, and
// shifted PETSCII bytes above 0x7F.
void test_trace_render(const char *what, const uint8_t *data, int len,
                       const char *exp_hex, const char *exp_text)
{
    char hex[SOFTIEC_TRACE_HEX_SIZE];
    char txt[SOFTIEC_TRACE_TEXT_SIZE];
    int hex_len = softiec_trace_hex(data, len, hex, sizeof(hex));
    int txt_len = softiec_trace_text(data, len, txt, sizeof(txt));
    bool ok = (strcmp(hex, exp_hex) == 0) && (strcmp(txt, exp_text) == 0) &&
              (hex_len == (int)strlen(exp_hex)) && (txt_len == (int)strlen(exp_text));
    if (ok) {
        printf("Trace %s => OK!\n", what);
        return;
    }
    printf("Trace %s rendered [%s] \"%s\", expected [%s] \"%s\"\n",
           what, hex, txt, exp_hex, exp_text);
    failures++;
}

// A rendering that does not fit has to say so rather than look like a short payload.
void test_trace_truncation(void)
{
    const uint8_t data[] = { 0x41, 0x42, 0x43, 0x44 };
    char hex[8];
    char txt[6];
    softiec_trace_hex(data, 4, hex, sizeof(hex));
    softiec_trace_text(data, 4, txt, sizeof(txt));
    if ((strcmp(hex, "41 42..") == 0) && (strcmp(txt, "ABC..") == 0)) {
        printf("Trace truncation => OK!\n");
        return;
    }
    printf("Trace truncation rendered [%s] \"%s\", expected [41 42..] \"ABC..\"\n", hex, txt);
    failures++;
}

void test_trace_formatters(void)
{
    const uint8_t empty[1] = { 0 };
    test_trace_render("empty payload", empty, 0, "", "");

    // The command that started #881: C, shifted P, and partition 13 as a byte.
    const uint8_t change_partition[] = { 'C', 0xD0, 0x0D };
    test_trace_render("binary change partition", change_partition, 3,
                      "43 D0 0D", "C\\xD0\\r");

    // A text command with the carriage return PRINT# appends behind it.
    const uint8_t text_command[] = { 'G', '-', 'P', 0x0D };
    test_trace_render("text command with terminator", text_command, 4,
                      "47 2D 50 0D", "G-P\\r");

    // An embedded zero is a payload byte here, not the end of the payload.
    const uint8_t with_zero[] = { 'A', 0x00, 'B' };
    test_trace_render("embedded zero", with_zero, 3, "41 00 42", "A\\0B");

    // Quotes and backslashes have to survive the quoted rendering.
    const uint8_t quoting[] = { '"', '\\', 0x0A };
    test_trace_render("quoting", quoting, 3, "22 5C 0A", "\\\"\\\\\\n");

    test_trace_truncation();
}
/* ==================== end of the #877 diagnostics tests ==================== */

int main(int argc, const char *argv[])
{
    test_trace_formatters(); // #877 diagnostics; removed with them

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
    // A position is at most four bytes wide, so five parameter bytes is a syntax error.
    test_command(ERR_SYNTAX, (const uint8_t *)"P\x02\xF4\x01\0\0\0", 7);
    test_command( 0, (const uint8_t *)"CD:TEMP", 7);
    test_command( 0, (const uint8_t *)"CD1//TEMP", 9);
    test_command( 0, (const uint8_t *)"CD1//TEMP/TEMP2", 15);
    test_command( 0, (const uint8_t *)"CD1_", 4);
    test_command( 0, (const uint8_t *)"RD33_/BLAH", 10);
    test_command( 0, (const uint8_t *)"CD/TEMP/TEMP2", 13);
    test_command( 0, (const uint8_t *)"T-RA", 4);
    test_command( 0, (const uint8_t *)"T-RI", 4);
    test_command( 0, (const uint8_t *)"T-RD", 4);
    test_command( 0, (const uint8_t *)"T-RB", 4);
    test_command( 0, (const uint8_t *)"T-WASUN. 13/07/25 09:34:13 PM", 29);
    test_command( 0, (const uint8_t *)"CP", 2);
    test_command( 0, (const uint8_t *)"CP1", 3);
    test_command( 0, (const uint8_t *)"CP12", 4);
    test_command( 0, (const uint8_t *)"CP123", 5);
    test_command( 0, (const uint8_t *)"CP1234", 6);
    test_command( 0, (const uint8_t *)"C\xD0\x1F", 3);
    test_command( 0, (const uint8_t *)"N3/INSOMEDIR:HELL/HEAVEN=D64,NAME OF DISK,XX", 44);
    test_command( 0, (const uint8_t *)"N:HELLO,123", 11);
    test_command( 0, (const uint8_t *)"N:HELLO,23", 10);
    test_command( 0, (const uint8_t *)"N:HELLO,A", 9);
    test_command( 0, (const uint8_t *)"N:HELLO,", 8);
    test_command( 0, (const uint8_t *)"N:HELLO", 7);
    test_command( 0, (const uint8_t *)"N:", 2);
    test_command( 0, (const uint8_t *)"MD:TEMP", 7);
    test_command( 0, (const uint8_t *)"MD1:TEMP", 8);
    test_command( 0, (const uint8_t *)"MD1//:TEMP", 10);
    test_command( 0, (const uint8_t *)"MD1//TEMP/:TEMP2", 16);
    test_command( 0, (const uint8_t *)"MD:", 3);
    test_command( 0, (const uint8_t *)"MD", 2);
    test_command( 0, (const uint8_t *)"MD/PATH\xC1\xC2", 9);
    test_command( 0, (const uint8_t *)"MD:PATH\xC1\xC2", 9);
    test_command( 0, (const uint8_t *)"XPWD", 4);

    if (failures) {
        printf("\n%d command parsing check(s) failed.\n", failures);
        return 1;
    }
    printf("\nAll command parsing checks passed.\n");
    return 0;
}
