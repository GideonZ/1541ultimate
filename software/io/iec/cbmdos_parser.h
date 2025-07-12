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
    e_folder,
} filetype_t;

typedef struct {
    int partition;
    mstring path;
    mstring filename;
    bool has_wildcard;
} filename_t;

typedef enum {
    e_stream_file,
    e_stream_buffer,
    e_stream_dir,
    e_stream_partitions,
} stream_type_t;

typedef enum {
    e_stamp_none,
    e_stamp_short,
    e_stamp_long,
} stamp_format_t;

typedef struct {
    stream_type_t stream;
    stamp_format_t timefmt;
    uint32_t min_datetime;
    uint32_t max_datetime;
    uint8_t filetypes; // P,S,U,R, B/D
} dir_options_t;

const dir_options_t c_dir_options_init = { e_stream_file, e_stamp_none, 0, 0, 0x00 };

typedef struct {
    filename_t file;
    bool replace;
    filetype_t filetype;
    fileaccess_t access;
    dir_options_t dir_opt;
    uint16_t record_size;
} open_t;

#define ERR_SYNTAX        30
#define ERR_ILLEGAL_CHARS 31
#define ERR_ILLEGAL_NAME  32
#define ERR_UNKNOWN_CMD   33

class IecCommandExecuter
{
public:
    virtual int do_block_read(int chan, int part, int track, int sector) { return 0; }
    virtual int do_block_write(int chan, int part, int track, int sector) { return 0; }
    virtual int do_block_allocate(int chan, int part, int track, int sector, bool allocate) { return 0; }
    virtual int do_buffer_position(int chan, int pos) { return 0; }
    virtual int do_set_current_partition(int part) { return 0; }
    virtual int do_change_dir(filename_t& dest) { return 0; }
    virtual int do_make_dir(filename_t& dest) { return 0; }
    virtual int do_remove_dir(filename_t& dest) { return 0; }
    virtual int do_copy(filename_t& dest, filename_t sources[], int n) { return 0; }
    virtual int do_initialize() { return 0; }
    virtual int do_format(uint8_t *name, uint8_t id1, uint8_t id2) { return 0; }
    virtual int do_rename(filename_t &src, filename_t &dest) { return 0; }
    virtual int do_scratch(filename_t filenames[], int n) { return 0; }
    virtual int do_cmd_response(uint8_t *data, int len) { return 0; }
    virtual int do_set_position(int chan, uint32_t pos, int recnr, int recoffset) { return 0; }
    virtual int do_pwd_command() { return 0; }

};

class IecParser
{
    IecCommandExecuter *exec;

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
    int extended_command(const uint8_t *buffer, int len);

public:
    IecParser(IecCommandExecuter *e) : exec(e) { }
    int execute_command(const uint8_t *buffer, int len);
};

int parse_open(const char *buf, open_t& fn);
int parse_full_path(const char *buf, filename_t& name, bool *replace, bool path_only);
int parse_dir_option(const char *buf, dir_options_t &opt);
const char *cbmdos_time(uint32_t dt, char *buf, bool longfmt);
const uint32_t make_fat_time(int y, int M, int d, int h, int m, int s);


#endif
