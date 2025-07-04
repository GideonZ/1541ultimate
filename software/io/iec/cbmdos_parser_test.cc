// Written by Gideon (2025)
// This module handles filename and command parsing for access to the VFS
// from the side of the commodore in a CMDHD / SD2IEC fashion

#include "cbmdos_parser.h"

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
                                            e_file, e_none, 0x0, 0x0, 0x00 };

IecCommandExecuter exec;
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
    printf("  Replace: %s\n", o.replace ? "true" : "false");
    printf("  FileType: %d\n", (int)o.filetype);
    printf("  Access: %d\n", (int)o.access);
    printf("  Dir: %s\n", o.dir_opt.stream == e_file ? "No" :
                        (o.dir_opt.stream == e_dir_files ? "Files" : "Partitions") );
    if (o.dir_opt.stream != e_file) {
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

void d_parse_open(const char *buf, open_t& o, int expected_retval = 0, open_result_t result = c_open_result_init)
{
    int err = parser.parse_open(buf, o);

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
        printf("Open '%s' is invalid: %d\n", buf, err);
    } else {
        printf("Open '%s' results in:\n", buf);
        print_open(o);
    }
    printf("\n");
}

int main(int argc, const char *argv[])
{
    open_t o;
    d_parse_open("JUSTFILE", o,     0,
                { -1, "", "JUSTFILE", false, false, e_any, e_not_set,
                  e_file, e_none, 0x0, 0x0, 0x00} );

    d_parse_open("JUSTFILE,U", o,   0,
                { -1, "", "JUSTFILE", false, false, e_usr, e_not_set,
                  e_file, e_none, 0x0, 0x0, 0x00} );

    d_parse_open("JUSTFILE,USR", o, ERR_ILLEGAL_CHARS );

    d_parse_open("JUSTFILE,U,A", o, 0,
                { -1, "", "JUSTFILE", false, false, e_usr, e_append,
                  e_file, e_none, 0x0, 0x0, 0x00 } );

    d_parse_open("JUSTFILE,H", o,  ERR_SYNTAX );

    d_parse_open("0:FILEON0", o,    0,
                {  0, "", "FILEON0",  false, false, e_any, e_not_set,
                  e_file, e_none, 0x0, 0x0, 0x00 } );

    d_parse_open("1:FILE/ON1", o,   0,
                {  1, "", "FILE/ON1", false, false, e_any, e_not_set,
                  e_file, e_none, 0x0, 0x0, 0x00 } );

    d_parse_open("-2:FILE ON NEGATIVE", o, ERR_SYNTAX );

    d_parse_open("56:FILE ON 56", o, 0,
                { 56, "", "FILE ON 56", false, false, e_any, e_not_set,
                  e_file, e_none, 0x0, 0x0, 0x00 } );

    d_parse_open("@:REPLACE", o,     0,
                { -1, "", "REPLACE",  false, true, e_any, e_not_set,
                  e_file, e_none, 0x0, 0x0, 0x00 } );

    d_parse_open("@1:REPLACE ON 1", o, 0,
                {  1, "", "REPLACE ON 1", false, true, e_any, e_not_set,
                  e_file, e_none, 0x0, 0x0, 0x00 } );

    d_parse_open("@1/HELLO:REPLACE ON 1 IN PATH HELLO", o, 0,
                { 1, "/HELLO", "REPLACE ON 1 IN PATH HELLO", false, true, e_any, e_not_set,
                  e_file, e_none, 0x0, 0x0, 0x00 });

    d_parse_open("@78/HELLO/DEEPER:EVERYTHING,U,A", o, 0,
                { 78, "/HELLO/DEEPER", "EVERYTHING", false, true, e_usr, e_append,
                  e_file, e_none, 0x0, 0x0, 0x00 });

    d_parse_open("//FROMROOT/DEEPER:BLAH,S,R", o, 0,
                { -1, "//FROMROOT/DEEPER", "BLAH", false, false, e_seq, e_read,
                  e_file, e_none, 0x0, 0x0, 0x00});

    d_parse_open("@345:", o, ERR_ILLEGAL_NAME);

    d_parse_open(",", o, ERR_ILLEGAL_CHARS);

    d_parse_open("BLAH,SEQ", o, ERR_ILLEGAL_CHARS);

    d_parse_open("@1:FOO,S", o, 0,
                { 1, "", "FOO", false, true, e_seq, e_not_set,
                  e_file, e_none, 0x0, 0x0, 0x00});

    d_parse_open("@1/HELLO?:DIR WITH WILDCARD", o, 0,
                { 1, "/HELLO?", "DIR WITH WILDCARD", false, true, e_any, e_not_set,
                  e_file, e_none, 0x0, 0x0, 0x00});

    d_parse_open("@1/HELLO:FILE WITH WILD*", o, 0,
                { 1, "/HELLO", "FILE WITH WILD*", true, true, e_any, e_not_set,
                  e_file, e_none, 0x0, 0x0, 0x00 });

    d_parse_open("$", o, 0,
                {-1, "", "", false, false, e_any, e_not_set,
                  e_dir_files, e_none, 0x0, 0x0, 0x00 });

    d_parse_open("$=T", o, 0,
                {-1, "", "", false, false, e_any, e_not_set,
                  e_dir_files, e_short, 0x0, 0x0, 0x00 });

    d_parse_open("$=P", o, 0,
                {-1, "", "", false, false, e_any, e_not_set,
                  e_dir_partitions, e_none, 0x0, 0x0, 0x00 });

    d_parse_open("$=T2", o, 0,
                { 2, "", "", false, false, e_any, e_not_set,
                  e_dir_files, e_short, 0x0, 0x0, 0x00 });

    d_parse_open("$=T2:*=P", o, 0,
                { 2, "", "*", true, false, e_any, e_not_set,
                  e_dir_files, e_short, 0x0, 0x0, 0x01 });

    d_parse_open("$=T2:*=P,L", o, 0,
                { 2, "", "*", true, false, e_any, e_not_set,
                  e_dir_files, e_long, 0x0, 0x0, 0x01 });

    d_parse_open("$=T2:*=P,L,>12/21/18 04:15 PM", o, 0,
                { 2, "", "*", true, false, e_any, e_not_set,
                  e_dir_files, e_long, 0x4D9581E0, 0x0, 0x01 });
                  // 2018=>38 => 0100110
                  // 12       => 1100
                  // 21       => 10101
                  // 16       => 10000
                  // 15       => 001111
                  // 00       => 00000
                  // 0100.1101.1001.0101.1000.0001.1110.0000  0x4D9581E0

    d_parse_open("$=T:*=L,<12/21/18 04:15 PM", o, 0, 
                { -1, "", "*", true, false, e_any, e_not_set,
                  e_dir_files, e_long, 0x0, 0x4D9581E0, 0x00 });

    d_parse_open("$=T4:*=S,N,>12/21/18 12:01 AM,<12/31/18 12:00 PM", o, 0, 
                { 4, "", "*", true, false, e_any, e_not_set,
                  e_dir_files, e_none, 0x4d950020, 0x4d9f6000, 0x02 });

    d_parse_open("$3", o, 0,
                { 3, "", "", false, false, e_any, e_not_set,
                  e_dir_files, e_none, 0x0, 0x00, 0x00 });

    parser.execute_command((const uint8_t *)"C:S=/C64 OS/:S", 14);
    parser.execute_command((const uint8_t *)"B-R 2 0 18 1\r", 13);
    parser.execute_command((const uint8_t *)"B-W 2 0 18 2\r", 13);
    parser.execute_command((const uint8_t *)"U1 2 0 18 3\r", 12);
    parser.execute_command((const uint8_t *)"U2 2 0 18 4\r", 12);
    parser.execute_command((const uint8_t *)"B-P 2 234\r", 10);
    parser.execute_command((const uint8_t *)"C99:EMPTY=", 10);
    parser.execute_command((const uint8_t *)"C1:FCOPY=3:FCOPY", 16);
    parser.execute_command((const uint8_t *)"C:FULLSTATS=STAT1,3:STAT3", 25);
    parser.execute_command((const uint8_t *)"C2:MCOPY=1/COPIERS/:MCOPY", 25);
    parser.execute_command((const uint8_t *)"COPY2/SUBDIR:COMBINED=1/COPIERS/:FILE1,1/COPIERS/:FILE2,2/OTHERDIR:FILE*", 73);
    parser.execute_command((const uint8_t *)"R1:BOOT1=BOOT", 13);
    parser.execute_command((const uint8_t *)"R1/UTILS/:NEWT=1/UTILS/:WW", 26);
    parser.execute_command((const uint8_t *)"S1:JUNK,3:C?*.BAS", 17);
    parser.execute_command((const uint8_t *)"S1/UTILS/:CO*", 13);
    parser.execute_command((const uint8_t *)"SCRATCH1/UTILS/:CO*", 19);
    parser.execute_command((const uint8_t *)"P\x02\x64", 3);
    parser.execute_command((const uint8_t *)"P\x02\xC8\0", 4);
    parser.execute_command((const uint8_t *)"P\x02\x2C\x01\0", 5);
    parser.execute_command((const uint8_t *)"P\x02\x90\x01\0\0", 6);
    parser.execute_command((const uint8_t *)"P\x02\xF4\x01\0\0\0", 7);
    parser.execute_command((const uint8_t *)"CD:TEMP", 7);
    parser.execute_command((const uint8_t *)"CD1//TEMP", 9);
    parser.execute_command((const uint8_t *)"CD1//TEMP/TEMP2", 15);
    parser.execute_command((const uint8_t *)"CD1_", 4);
    parser.execute_command((const uint8_t *)"RD33_/BLAH", 10);
    parser.execute_command((const uint8_t *)"CD/TEMP/TEMP2", 13);
    parser.execute_command((const uint8_t *)"T-RA", 4);
    parser.execute_command((const uint8_t *)"T-RI", 4);
    parser.execute_command((const uint8_t *)"T-RD", 4);
    parser.execute_command((const uint8_t *)"T-RB", 4);
    parser.execute_command((const uint8_t *)"CP1", 3);
    parser.execute_command((const uint8_t *)"CP12", 4);
    parser.execute_command((const uint8_t *)"CP123", 5);
    parser.execute_command((const uint8_t *)"CP1234", 6);
    parser.execute_command((const uint8_t *)"C\xD0\x1F", 3);
    parser.execute_command((const uint8_t *)"N:HELLO,123", 11);
    parser.execute_command((const uint8_t *)"N:HELLO,23", 10);
    parser.execute_command((const uint8_t *)"N:HELLO,A", 9);
    parser.execute_command((const uint8_t *)"N:HELLO,", 8);
    parser.execute_command((const uint8_t *)"N:HELLO", 7);
    parser.execute_command((const uint8_t *)"N:", 2);
    parser.execute_command((const uint8_t *)"MD:TEMP", 7);
    parser.execute_command((const uint8_t *)"MD1:TEMP", 8);
    parser.execute_command((const uint8_t *)"MD1//:TEMP", 10);
    parser.execute_command((const uint8_t *)"MD1//TEMP/:TEMP2", 16);
    parser.execute_command((const uint8_t *)"MD:", 3);
    parser.execute_command((const uint8_t *)"MD", 2);

}
