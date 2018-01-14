/*
    $Id: arguments.c 1512 2017-05-01 08:19:51Z soci $

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#include "arguments.h"
#include <string.h>
#include <ctype.h>
#include "64tass.h"
#include "opcodes.h"
#include "my_getopt.h"
#include "file.h"
#include "error.h"
#include "unicode.h"
#include "wchar.h"

struct arguments_s arguments = {
    true,        /* warning */
    true,        /* caret */
    true,        /* quiet */
    false,       /* to_ascii */
    true,        /* monitor */
    true,        /* source */
    false,       /* linenum */
    false,       /* longbranch */
    false,       /* tasmcomp */
    false,       /* verbose */
    0x20,        /* caseinsensitive */
    {            /* output */
        "a.out",     /* name */
        OUTPUT_CBM,  /* mode */
        false        /* longaddr */
    },
    &c6502,      /* cpumode */
    NULL,        /* symbol_output */
    0,           /* symbol_output_len */
    NULL,        /* list */
    NULL,        /* make */
    NULL,        /* error */
    8,           /* tab_size */
};

struct diagnostics_s diagnostics = {
    false,       /* shadow */
    false,       /* strict_bool */
    false,       /* optimize */
    false,       /* implied_reg */
    true,        /* jmp_bug */
    true,        /* pc_wrap */
    true,        /* mem_wrap */
    true,        /* label_left */
    false,       /* branch_page */
    true,        /* deprecated */
    false,       /* old_equal */
    true,        /* portable */
    {
        false,   /* unused-macro */
        false,   /* unused-const */
        false,   /* unused-label */
        false,   /* unused-variable */
    },
    false,       /* case_symbol */
    false,       /* switch_case */
    false,       /* immediate */
    true,        /* float_compare */
    false,       /* leading_zeros */
    false,       /* alias */
    true,        /* pitfalls */
    true,        /* star_assign */
    true,        /* ignored */
    false        /* long_branch */
};

struct diagnostics_s diagnostic_errors = {
    false,       /* shadow */
    false,       /* strict_bool */
    false,       /* optimize */
    false,       /* implied_reg */
    false,       /* jmp_bug */
    false,       /* pc_wrap */
    false,       /* mem_wrap */
    false,       /* label_left */
    false,       /* branch_page */
    false,       /* deprecated */
    false,       /* old_equal */
    false,       /* portable */
    {
        false,   /* unused-macro */
        false,   /* unused-const */
        false,   /* unused-label */
        false,   /* unused-variable */
    },
    false,       /* case_symbol */
    false,       /* switch_case */
    false,       /* immediate */
    false,       /* float_compare */
    false,       /* leading_zeros */
    false,       /* alias */
    false,       /* pitfalls */
    false,       /* star_assign */
    false,       /* ignored */
    false        /* long_branch */
};

static struct diagnostics_s diagnostic_no_all;
static struct diagnostics_s diagnostic_all = {
    true,        /* shadow */
    true,        /* strict_bool */
    false,       /* optimize */
    true,        /* implied_reg */
    true,        /* jmp_bug */
    true,        /* pc_wrap */
    true,        /* mem_wrap */
    true,        /* label_left */
    false,       /* branch_page */
    true,        /* deprecated */
    true,        /* old_equal */
    true,        /* portable */
    {
        false,   /* unused-macro */
        false,   /* unused-const */
        false,   /* unused-label */
        false,   /* unused-variable */
    },
    false,       /* case_symbol */
    true,        /* switch_case */
    false,       /* immediate */
    true,        /* float_compare */
    true,        /* leading_zeros */
    false,       /* alias */
    true,        /* pitfalls */
    true,        /* star_assign */
    true,        /* ignored */
    false        /* long_branch */
};

static struct diagnostics_s diagnostic_no_error_all;
static struct diagnostics_s diagnostic_error_all = {
    true,        /* shadow */
    true,        /* strict_bool */
    true,        /* optimize */
    true,        /* implied_reg */
    true,        /* jmp_bug */
    true,        /* pc_wrap */
    true,        /* mem_wrap */
    true,        /* label_left */
    true,        /* branch_page */
    true,        /* deprecated */
    true,        /* old_equal */
    true,        /* portable */
    {
        true,    /* unused-macro */
        true,    /* unused-const */
        true,    /* unused-label */
        true,    /* unused-variable */
    },
    true,        /* case_symbol */
    true,        /* switch_case */
    true,        /* immediate */
    true,        /* float_compare */
    true,        /* leading_zeros */
    true,        /* alias */
    true,        /* pitfalls */
    true,        /* star_assign */
    true,        /* ignored */
    true         /* long_branch */
};

struct w_options_s {
    const char *name;
    bool *opt;
};

static const struct w_options_s w_options[] = {
    {"optimize",        &diagnostics.optimize},
    {"shadow",          &diagnostics.shadow},
    {"strict-bool",     &diagnostics.strict_bool},
    {"implied-reg",     &diagnostics.implied_reg},
    {"jmp-bug",         &diagnostics.jmp_bug},
    {"pc-wrap",         &diagnostics.pc_wrap},
    {"mem-wrap",        &diagnostics.mem_wrap},
    {"label-left",      &diagnostics.label_left},
    {"branch-page",     &diagnostics.branch_page},
    {"deprecated",      &diagnostics.deprecated},
    {"old-equal",       &diagnostics.old_equal},
    {"portable",        &diagnostics.portable},
    {"unused-macro",    &diagnostics.unused.macro},
    {"unused-const",    &diagnostics.unused.consts},
    {"unused-label",    &diagnostics.unused.label},
    {"unused-variable", &diagnostics.unused.variable},
    {"case-symbol",     &diagnostics.case_symbol},
    {"switch-case",     &diagnostics.switch_case},
    {"immediate",       &diagnostics.immediate},
    {"float-compare",   &diagnostics.float_compare},
    {"leading-zeros",   &diagnostics.leading_zeros},
    {"alias",           &diagnostics.alias},
    {"pitfalls",        &diagnostics.pitfalls},
    {"star-assign",     &diagnostics.star_assign},
    {"ignored",         &diagnostics.ignored},
    {"long-branch",     &diagnostics.long_branch},
    {NULL,              NULL}
};

static bool woption(const char *s) {
    bool no = (s[0] == 'n') && (s[1] == 'o') && (s[2] == '-'), *b;
    const struct w_options_s *w = w_options;
    const char *s2 = no ? s + 3 : s;
    size_t m;

    if (strcmp(s2, "all") == 0) {
        memcpy(&diagnostics, no ? &diagnostic_no_all : &diagnostic_all, sizeof diagnostics);
        return false;
    }

    if (strcmp(s2, "error") == 0) {
        memcpy(&diagnostic_errors, no ? &diagnostic_no_error_all : &diagnostic_error_all, sizeof diagnostic_errors);
        return false;
    }

    m = strcmp(s2, "unused") == 0 ? strlen(s2) : SIZE_MAX;

    if (strncmp(s2, "error=", 6) == 0) {
        s2 += 6;
        while (w->name != NULL) {
            if (strncmp(w->name, s2, m) == 0) {
                if (!no) *w->opt = true;
                b = w->opt - &diagnostics.shadow + &diagnostic_errors.shadow;
                *b = !no;
                b = w->opt - &diagnostics.shadow + &diagnostic_error_all.shadow;
                *b = !no;
                b = w->opt - &diagnostics.shadow + &diagnostic_no_error_all.shadow;
                *b = !no;
                if (m == SIZE_MAX) return false;
            }
            w++;
        }
    } else {
        while (w->name != NULL) {
            if (strncmp(w->name, s2, m) == 0) {
                *w->opt = !no;
                b = w->opt - &diagnostics.shadow + &diagnostic_all.shadow;
                *b = !no;
                b = w->opt - &diagnostics.shadow + &diagnostic_no_all.shadow;
                *b = !no;
                if (m == SIZE_MAX) return false;
            }
            w++;
        }
    }
    if (m != SIZE_MAX) return false;
    fatal_error("unrecognized option '-W");
    printable_print((const uint8_t *)s, stderr);
    putc('\'', stderr);
    fatal_error(NULL);
    return true;
}

static const char *short_options = "wqnbfXaTCBicxtel:L:I:M:msV?o:D:E:W:";

static const struct my_option long_options[] = {
    {"no-warn"          , my_no_argument      , NULL, 'w'},
    {"quiet"            , my_no_argument      , NULL, 'q'},
    {"nonlinear"        , my_no_argument      , NULL, 'n'},
    {"nostart"          , my_no_argument      , NULL, 'b'},
    {"flat"             , my_no_argument      , NULL, 'f'},
    {"long-address"     , my_no_argument      , NULL, 'X'},
    {"atari-xex"        , my_no_argument      , NULL,  0x107},
    {"apple-ii"         , my_no_argument      , NULL,  0x108},
    {"intel-hex"        , my_no_argument      , NULL,  0x10e},
    {"s-record"         , my_no_argument      , NULL,  0x10f},
    {"cbm-prg"          , my_no_argument      , NULL,  0x10c},
    {"ascii"            , my_no_argument      , NULL, 'a'},
    {"tasm-compatible"  , my_no_argument      , NULL, 'T'},
    {"case-sensitive"   , my_no_argument      , NULL, 'C'},
    {"long-branch"      , my_no_argument      , NULL, 'B'},
    {"m65xx"            , my_no_argument      , NULL,  0x101},
    {"m6502"            , my_no_argument      , NULL, 'i'},
    {"m65c02"           , my_no_argument      , NULL, 'c'},
    {"m65ce02"          , my_no_argument      , NULL,  0x106},
    {"m65816"           , my_no_argument      , NULL, 'x'},
    {"m65dtv02"         , my_no_argument      , NULL, 't'},
    {"m65el02"          , my_no_argument      , NULL, 'e'},
    {"mr65c02"          , my_no_argument      , NULL,  0x104},
    {"mw65c02"          , my_no_argument      , NULL,  0x105},
    {"m4510"            , my_no_argument      , NULL,  0x111},
    {"labels"           , my_required_argument, NULL, 'l'},
    {"output"           , my_required_argument, NULL, 'o'},
    {"error"            , my_required_argument, NULL, 'E'},
    {"vice-labels"      , my_no_argument      , NULL,  0x10b},
    {"dump-labels"      , my_no_argument      , NULL,  0x10d},
    {"labels-root"      , my_required_argument, NULL,  0x113},
    {"list"             , my_required_argument, NULL, 'L'},
    {"verbose-list"     , my_no_argument      , NULL,  0x110},
    {"no-monitor"       , my_no_argument      , NULL, 'm'},
    {"no-source"        , my_no_argument      , NULL, 's'},
    {"line-numbers"     , my_no_argument      , NULL,  0x112},
    {"no-caret-diag"    , my_no_argument      , NULL,  0x10a},
    {"tab-size"         , my_required_argument, NULL,  0x109},
    {"version"          , my_no_argument      , NULL, 'V'},
    {"usage"            , my_no_argument      , NULL,  0x102},
    {"help"             , my_no_argument      , NULL,  0x103},
    {NULL               , my_no_argument      , NULL,  0}
};

static MUST_CHECK char *read_one(FILE *f) {
    bool q, q2, q3;
    char *line;
    size_t i, ln, n, j, len;
    int c;
    mbstate_t ps;
    uint8_t *p, *data;

    do {
        c = getc(f);
        if (c == EOF) break;
    } while (c == 0 || isspace(c) != 0);
    if (c == EOF) return NULL;
    line = NULL;
    i = ln = 0;
    q = q2 = q3 = false;
    do {
        if (!q3 && c == '\\') q3 = true;
        else if (!q3 && !q2 && c == '"') q = !q;
        else if (!q3 && !q && c == '\'') q2 = !q2;
        else {
            q3 = false;
            if (i >= ln) {
                ln += 16;
                line = (char *)realloc(line, ln);
                if (ln < 16 || line == NULL) err_msg_out_of_memory2();
            }
            line[i++] = (char)c;
        }
        c = getc(f);
        if (c == EOF || c == 0) break;
    } while (q || q2 || q3 || isspace(c) == 0);
    if (i >= ln) {
        ln++;
        line = (char *)realloc(line, ln);
        if (ln < 1 || line == NULL) err_msg_out_of_memory2();
    }
    line[i] = 0;

    n = i, j = 0;
    len = n + 64;
    data = (uint8_t *)malloc(len);
    if (data == NULL || len < 64) err_msg_out_of_memory2();

    memset(&ps, 0, sizeof ps);
    p = data;
    for (;;) {
        ssize_t l;
        wchar_t w;
        uchar_t ch;
        if (p + 6*6 + 1 > data + len) {
            size_t o = (size_t)(p - data);
            len += 1024;
            data = (uint8_t*)realloc(data, len);
            if (data == NULL) err_msg_out_of_memory2();
            p = data + o;
        }
        l = (ssize_t)mbrtowc(&w, line + j, n - j,  &ps);
        if (l < 1) {
            w = (uint8_t)line[j];
            if (w == 0 || l == 0) break;
            l = 1;
        }
        j += (size_t)l;
        ch = (uchar_t)w;
        if (ch != 0 && ch < 0x80) *p++ = (uint8_t)ch; else p = utf8out(ch, p);
    }
    *p++ = 0;
    free(line);
    return (char *)data;
}

int testarg(int *argc2, char **argv2[], struct file_s *fin) {
    int argc = *argc2;
    char **argv = *argv2;
    int opt, tab;
    size_t max_lines = 0, fp = 0;
    int max = 10;
    bool again;
    struct symbol_output_s symbol_output = { NULL, LABEL_64TASS, NULL };

    do {
        int i;
        again = false;
        for (;;) {
            opt = my_getopt_long(argc, argv, short_options, long_options, NULL);
            if (opt == -1) break;
            switch (opt) {
            case 'W':
                if (woption(my_optarg)) goto exit;
                break;
            case 'w':arguments.warning = false;break;
            case 'q':arguments.quiet = false;break;
            case 'X':arguments.output.longaddr = true;break;
            case 'n':arguments.output.mode = OUTPUT_NONLINEAR;break;
            case 0x107:arguments.output.mode = OUTPUT_XEX;break;
            case 0x108:arguments.output.mode = OUTPUT_APPLE;break;
            case 0x10e:arguments.output.mode = OUTPUT_IHEX;break;
            case 0x10f:arguments.output.mode = OUTPUT_SREC;break;
            case 0x10c:arguments.output.mode = OUTPUT_CBM;break;
            case 'b':arguments.output.mode = OUTPUT_RAW;break;
            case 'f':arguments.output.mode = OUTPUT_FLAT;break;
            case 'a':arguments.to_ascii = true;break;
            case 'T':arguments.tasmcomp = true;break;
            case 'o':arguments.output.name = my_optarg;break;
            case 0x10a:arguments.caret = false;break;
            case 'D':
                {
                    size_t len = strlen(my_optarg) + 1;

                    if (fin->lines >= max_lines) {
                        max_lines += 1024;
                        if (/*max_lines < 1024 ||*/ max_lines > SIZE_MAX / sizeof *fin->line) err_msg_out_of_memory(); /* overflow */
                        fin->line = (size_t *)reallocx(fin->line, max_lines * sizeof *fin->line);
                    }
                    fin->line[fin->lines++] = fp;

                    if (len < 1 || fp + len < len) err_msg_out_of_memory();
                    if (fp + len > fin->len) {
                        fin->len = fp + len + 1024;
                        if (fin->len < 1024) err_msg_out_of_memory();
                        fin->data = (uint8_t*)reallocx(fin->data, fin->len);
                    }
                    memcpy(fin->data + fp, my_optarg, len);
                    fp += len;
                }
                break;
            case 'B': arguments.longbranch = true;break;
            case 0x101: arguments.cpumode = &c6502;break;
            case 'i': arguments.cpumode = &c6502i;break;
            case 'c': arguments.cpumode = &c65c02;break;
            case 0x106: arguments.cpumode = &c65ce02;break;
            case 'x': arguments.cpumode = &w65816;break;
            case 't': arguments.cpumode = &c65dtv02;break;
            case 'e': arguments.cpumode = &c65el02;break;
            case 0x104: arguments.cpumode = &r65c02;break;
            case 0x105: arguments.cpumode = &w65c02;break;
            case 0x111: arguments.cpumode = &c4510;break;
            case 'l': symbol_output.name = my_optarg;
                      arguments.symbol_output_len++;
                      arguments.symbol_output = (struct symbol_output_s *)reallocx(arguments.symbol_output, arguments.symbol_output_len * sizeof *arguments.symbol_output);
                      arguments.symbol_output[arguments.symbol_output_len - 1] = symbol_output;
                      symbol_output.mode = LABEL_64TASS;
                      symbol_output.space = NULL;
                      break;
            case 0x10b: symbol_output.mode = LABEL_VICE; break;
            case 0x10d: symbol_output.mode = LABEL_DUMP; break;
            case 0x113: symbol_output.space = my_optarg; break;
            case 'E': arguments.error = my_optarg;break;
            case 'L': arguments.list = my_optarg;break;
            case 'M': arguments.make = my_optarg;break;
            case 'I': include_list_add(my_optarg);break;
            case 'm': arguments.monitor = false;break;
            case 's': arguments.source = false;break;
            case 0x112: arguments.linenum = true;break;
            case 'C': arguments.caseinsensitive = 0;break;
            case 0x110: arguments.verbose = true;break;
            case 0x109:tab = atoi(my_optarg); if (tab > 0 && tab <= 64) arguments.tab_size = (unsigned int)tab; break;
            case 0x102:puts(
             /* 12345678901234567890123456789012345678901234567890123456789012345678901234567890 */
               "Usage: 64tass [-abBCfnTqwWcitxmse?V] [-D <label>=<value>] [-o <file>]\n"
               "        [-E <file>] [-I <path>] [-l <file>] [-L <file>] [-M <file>] [--ascii]\n"
               "        [--nostart] [--long-branch] [--case-sensitive] [--cbm-prg] [--flat]\n"
               "        [--atari-xex] [--apple-ii] [--intel-hex] [--s-record] [--nonlinear]\n"
               "        [--tasm-compatible] [--quiet] [--no-warn] [--long-address] [--m65c02]\n"
               "        [--m6502] [--m65xx] [--m65dtv02] [--m65816] [--m65el02] [--mr65c02]\n"
               "        [--mw65c02] [--m65ce02] [--m4510] [--labels=<file>] [--vice-labels]\n"
               "        [--dump-labels] [--list=<file>] [--no-monitor] [--no-source]\n"
               "        [--line-numbers] [--tab-size=<value>] [--verbose-list] [-W<option>]\n"
               "        [--errors=<file>] [--output=<file>] [--help] [--usage]\n"
               "        [--version] SOURCES");
                   return 0;

            case 'V':puts("64tass Turbo Assembler Macro V" VERSION);
                     return 0;
            case 0x103:
            case '?':if (my_optopt == '?' || opt == 0x103) { puts(
               "Usage: 64tass [OPTIONS...] SOURCES\n"
               "64tass Turbo Assembler Macro V" VERSION "\n"
               "\n"
               "  -a, --ascii           Source is not in PETASCII\n"
               "  -B, --long-branch     Automatic bxx *+3 jmp $xxxx\n"
               "  -C, --case-sensitive  Case sensitive labels\n"
               "  -D <label>=<value>    Define <label> to <value>\n"
               "  -E, --error=<file>    Place errors into <file>\n"
               "  -I <path>             Include search path\n"
               "  -M <file>             Makefile dependencies to <file>\n"
               "  -q, --quiet           Do not output summary and header\n"
               "  -T, --tasm-compatible Enable TASM compatible mode\n"
               "  -w, --no-warn         Suppress warnings\n"
               "      --no-caret-diag   Suppress source line display\n"
               "\n"
               " Diagnostic options:\n"
               "  -Wall                 Enable most diagnostic warnings\n"
               "  -Werror               Diagnostic warnings to errors\n"
               "  -Werror=<name>        Make a diagnostic to an error\n"
               "  -Wno-error=<name>     Make a diagnostic to a warning\n"
               "  -Walias               Warn about instruction aliases\n"
               "  -Wbranch-page         Warn if a branch crosses a page\n"
               "  -Wcase-symbol         Warn on mismatch of symbol case\n"
               "  -Wimmediate           Suggest immediate addressing\n"
               "  -Wimplied-reg         No implied register aliases\n"
               "  -Wleading-zeros       Warn for ignored leading zeros\n"
               "  -Wlong-branch         Warn when a long branch is used\n"
               "  -Wno-deprecated       No deprecated feature warnings\n"
               "  -Wno-float-compare    No approximate compare warnings\n"
               "  -Wno-ignored          No directive ignored warnings\n"
               "  -Wno-jmp-bug          No jmp ($xxff) bug warning\n"
               "  -Wno-label-left       No warning about strange labels\n"
               "  -Wno-mem-wrap         No offset overflow warning\n"
               "  -Wno-pc-wrap          No PC overflow warning\n"
               "  -Wno-pitfalls         No common pitfall notes\n"
               "  -Wno-portable         No portability warnings\n"
               "  -Wno-star-assign      No label multiply warnings\n"
               "  -Wold-equal           Warn about old equal operator\n"
               "  -Woptimize            Optimization warnings\n"
               "  -Wshadow              Check symbol shadowing\n"
               "  -Wstrict-bool         No implicit bool conversions\n"
               "  -Wswitch-case         Warn about ignored cases\n"
               "  -Wunused              Warn about unused symbols\n"
               "  -Wunused-macro        Warn about unused macros\n"
               "  -Wunused-const        Warn about unused consts\n"
               "  -Wunused-label        Warn about unused labels\n"
               "  -Wunused-variable     Warn about unused variables\n"
               "\n"
               " Output selection:\n"
               "  -o, --output=<file>   Place output into <file>\n"
               "  -b, --nostart         Strip starting address\n"
               "  -f, --flat            Generate flat output file\n"
               "  -n, --nonlinear       Generate nonlinear output file\n"
               "  -X, --long-address    Use 3 byte start/len address\n"
               "      --cbm-prg         Output CBM program file\n"
               "      --atari-xex       Output Atari XEX file\n"
               "      --apple-ii        Output Apple II file\n"
               "      --intel-hex       Output Intel HEX file\n"
               "      --s-record        Output Motorola S-record file\n"
               "\n"
               " Target CPU selection:\n"
               "      --m65xx           Standard 65xx (default)\n"
               "  -c, --m65c02          CMOS 65C02\n"
               "      --m65ce02         CSG 65CE02\n"
               "  -e, --m65el02         65EL02\n"
               "  -i, --m6502           NMOS 65xx\n"
               "  -t, --m65dtv02        65DTV02\n"
               "  -x, --m65816          W65C816\n"
               "      --mr65c02         R65C02\n"
               "      --mw65c02         W65C02\n"
               "      --m4510           CSG 4510\n"
               "\n"
               " Source listing and labels:\n"
               "  -l, --labels=<file>   List labels into <file>\n"
               "      --vice-labels     Labels in VICE format\n"
               "      --dump-labels     Dump for debugging\n"
               "      --labels-root=<l> List from scope <l> only\n"
               "  -L, --list=<file>     List into <file>\n"
               "  -m, --no-monitor      Don't put monitor code into listing\n"
               "  -s, --no-source       Don't put source code into listing\n"
               "      --line-numbers    Put line numbers into listing\n"
               "      --tab-size=<n>    Override the default tab size (8)\n"
               "      --verbose-list    List unused lines as well\n"
               "\n"
               " Misc:\n"
               "  -?, --help            Give this help list\n"
               "      --usage           Give a short usage message\n"
               "  -V, --version         Print program version\n"
               "\n"
               "Mandatory or optional arguments to long options are also mandatory or optional\n"
               "for any corresponding short options.\n"
               "\n"
               "Report bugs to <soci" "\x40" "c64.rulez.org>.");
               return 0;
            }
                /* fall through */
            default:
            exit:
                fputs("Try '64tass --help' or '64tass --usage' for more information.\n", stderr);
                return -1;
            }
        }

        if (my_optind > 1 && strcmp(argv[my_optind - 1], "--") == 0) break;
        for (i = my_optind; i < argc; i++) {
            char *arg = argv[i];
            if (arg[0] == '@' && arg[1] != 0) {
                FILE *f = fopen(arg+1, "rb");
                if (f != NULL) {
                    int j;
                    free(arg);
                    argc--;
                    for (j = i; j < argc; j++) {
                        argv[j] = argv[j + 1];
                    }
                    while (feof(f) == 0) {
                        char *onepar = read_one(f);
                        if (onepar == NULL) break;
                        *argv2 = argv = (char **)reallocx(argv, ((size_t)argc + 1) * sizeof *argv);
                        for (j = argc; j > i; j--) {
                            argv[j] = argv[j - 1];
                        }
                        argc++;
                        argv[i++] = onepar;
                        again = true;
                    }
                    fclose(f);
                    *argc2 = argc;
                    break;
                }
            }
        }
        max--;
    } while (again && max > 0);

    if (again && max <= 0) {
        fatal_error("too many @-files encountered");
        fatal_error(NULL);
        return -1;
    }

    if (arguments.symbol_output_len > 0) {
        if (symbol_output.mode != LABEL_64TASS) arguments.symbol_output[arguments.symbol_output_len - 1].mode = symbol_output.mode;
        if (symbol_output.space != NULL) arguments.symbol_output[arguments.symbol_output_len - 1].space = symbol_output.space;
    }

    switch (arguments.output.mode) {
    case OUTPUT_RAW:
    case OUTPUT_NONLINEAR:
    case OUTPUT_CBM: all_mem2 = arguments.output.longaddr ? 0xffffff : 0xffff; break;
    case OUTPUT_IHEX:
    case OUTPUT_SREC:
    case OUTPUT_FLAT: all_mem2 = 0xffffffff; break;
    case OUTPUT_APPLE:
    case OUTPUT_XEX: all_mem2 = 0xffff; break;
    }
    if (arguments.caseinsensitive == 0) {
        diagnostics.case_symbol = false;
    }
    if (dash_name(arguments.output.name)) arguments.quiet = false;
    if (fin->lines != max_lines) {
        size_t *d = (size_t *)realloc(fin->line, fin->lines * sizeof *fin->line);
        if (fin->lines == 0 || d != NULL) fin->line = d;
    }
    closefile(fin);
    if (fp != fin->len) {
        fin->len = fp;
        if (fp != 0) {
            uint8_t *d = (uint8_t *)realloc(fin->data, fp);
            if (d != NULL) fin->data = d;
        }
    }
    fin->encoding = E_UTF8;
    if (argc <= my_optind) {
        fputs("Usage: 64tass [OPTIONS...] SOURCES\n"
              "Try '64tass --help' or '64tass --usage' for more information.\n", stderr);
        return -1;
    }
    return my_optind;
}
