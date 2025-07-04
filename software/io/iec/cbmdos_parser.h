#ifndef CBMDOS_PARSER_H
#define CBMDOS_PARSER_H

#include "mystring.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

typedef enum {
    e_not_set,
    e_read,
    e_write,
    e_append,
} fileaccess_t;

typedef enum {
    e_any,
    e_prg,
    e_seq,
    e_usr,
    e_rel,
} filetype_t;

typedef struct {
    int partition;
    mstring path;
    mstring filename;
    bool has_wildcard;
} filename_t;

typedef enum {
    e_file,
    e_dir_files,
    e_dir_partitions,
} stream_type_t;

typedef enum {
    e_none,
    e_short,
    e_long,
} stamp_format_t;

typedef struct {
    stream_type_t stream;
    stamp_format_t timefmt;
    uint32_t min_datetime;
    uint32_t max_datetime;
    uint8_t filetypes; // P,S,U,R, B/D
} dir_options_t;

const dir_options_t c_dir_options_init = { e_file, e_none, 0, 0, 0x00 };

typedef struct {
    filename_t file;
    bool replace;
    filetype_t filetype;
    fileaccess_t access;
    dir_options_t dir_opt;
} open_t;

#define ERR_SYNTAX        30
#define ERR_ILLEGAL_CHARS 31
#define ERR_ILLEGAL_NAME  32
#define ERR_UNKNOWN_CMD   33

class IecCommandExecuter
{
public:
    int do_block_read(int chan, int part, int track, int sector);
    int do_block_write(int chan, int part, int track, int sector);
    int do_buffer_position(int chan, int pos);
    int do_set_current_partition(int part);
    int do_change_dir(int part, mstring& path);
    int do_make_dir(int part, mstring& path);
    int do_remove_dir(int part, mstring& path);
    int do_copy(filename_t& dest, filename_t sources[], int n);
    int do_initialize();
    int do_format(uint8_t *name, uint8_t id1, uint8_t id2);
    int do_rename(filename_t &src, filename_t &dest);
    int do_scratch(filename_t filenames[], int n);
    int do_cmd_response(uint8_t *data, int len);
    int do_set_position(int chan, uint32_t pos);
};

class IecParser
{
    IecCommandExecuter *exec;

    int parse_full_path(const char *buf, filename_t& name, bool *replace, bool path_only);
    int parse_dir_option(const char *buf, dir_options_t &opt);

    int block_command(const uint8_t *buffer, int len);
    int cp_command(const uint8_t *buffer, int len);
    int dir_command(const uint8_t *buffer, int len);
    int copy_command(const uint8_t *buffer, int len);
    int initialize_command(const uint8_t *buffer, int len);
    int format_command(const uint8_t *buffer, int len);
    int position_command(const uint8_t *buffer, int len);
    int rename_command(const uint8_t *buffer, int len);
    int scratch_command(const uint8_t *buffer, int len);
    int time_command(const uint8_t *buffer, int len);
    int user_command(const uint8_t *buffer, int len);

public:
    IecParser(IecCommandExecuter *e) : exec(e) { }
    int parse_open(const char *buf, open_t& fn);
    int execute_command(const uint8_t *buffer, int len);
};

const char *cbmdos_time(uint32_t dt, char *buf, bool longfmt);
const uint32_t make_fat_time(int y, int M, int d, int h, int m, int s);


#endif
