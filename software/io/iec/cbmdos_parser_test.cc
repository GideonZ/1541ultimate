// Written by Gideon (2025)
// This module handles filename and command parsing for access to the VFS
// from the side of the commodore in a CMDHD / SD2IEC fashion

#include "cbmdos_parser.h"
#include "pattern.h"

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
} open_result_t;

const open_result_t c_open_result_init = { 0, "", "", false, false, e_any, e_not_set,
                                            e_stream_file, e_stamp_none, 0x0, 0x0, 0x00 };

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
    test_dispatch("B-A:2,0,16,3", 12, 0, "block allocate", 2, 0, 16, 3);
    test_dispatch("B-F:2,0,16,4", 12, 0, "block free", 2, 0, 16, 4);
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
    // U3 to U8 jump into a drive buffer, which has no equivalent here.
    test_dispatch("U3:2,0,18,0", 11, ERR_UNKNOWN_CMD, NULL);

    // The position command takes the secondary address either on its own or with 96
    // added to it, which is the form the manuals document. Three parameter bytes are
    // both a byte position and a record number with an offset; the drive decides
    // which of the two it uses from the state of the channel.
    test_dispatch("P\x02\x01\x00\x01", 5, 0, "set position", 2, 0x10001, 1, 1);
    test_dispatch("P\x62\x01\x00\x01", 5, 0, "set position", 0x62, 0x10001, 1, 1);
}

int main(int argc, const char *argv[])
{
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

    d_parse_open("@345:", o, ERR_ILLEGAL_NAME);

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
    test_command(32, (const uint8_t *)"C99:EMPTY=", 10);
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
