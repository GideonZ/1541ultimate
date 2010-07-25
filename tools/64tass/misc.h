/*

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/
#ifndef _MISC_H_
#define _MISC_H_
// ---------------------------------------------------------------------------
// $00-$3f warning
// $40-$7f error
// $80-$bf fatal
#define ERROR_TOP_OF_MEMORY 0x00
#define ERROR_A_USED_AS_LBL 0x01
#define ERROR___BANK_BORDER 0x02
#define ERROR______JUMP_BUG 0x03
#define ERROR___LONG_BRANCH 0x04
#define ERROR_WUSER_DEFINED 0x05

#define ERROR_DOUBLE_DEFINE 0x40
#define ERROR___NOT_DEFINED 0x41
#define ERROR_EXTRA_CHAR_OL 0x42
#define ERROR_CONSTNT_LARGE 0x43
#define ERROR_GENERL_SYNTAX 0x44
#define ERROR______EXPECTED 0x45
#define ERROR_EXPRES_SYNTAX 0x46
#define ERROR_BRANCH_TOOFAR 0x47
#define ERROR_MISSING_ARGUM 0x48
#define ERROR_ILLEGAL_OPERA 0x49
#define ERROR_UNKNOWN_ENCOD 0x4A
#define ERROR_REQUIREMENTS_ 0x4B
#define ERROR______CONFLICT 0x4C
#define ERROR_DIVISION_BY_Z 0x4D
#define ERROR____PAGE_ERROR 0x4E

#define ERROR_CANT_FINDFILE 0x80
#define ERROR_OUT_OF_MEMORY 0x81
#define ERROR_CANT_WRTE_OBJ 0x82
#define ERROR_LINE_TOO_LONG 0x83
#define ERROR_CANT_DUMP_LST 0x84
#define ERROR_CANT_DUMP_LBL 0x85
#define ERROR__USER_DEFINED 0x86
#define ERROR_FILERECURSION 0x87
#define ERROR__MACRECURSION 0x88
#define ERROR___UNKNOWN_CPU 0x89

#define WHAT_EXPRESSION 1
#define WHAT_MNEMONIC   2
#define WHAT_HASHMARK   3
#define WHAT_X          4
#define WHAT_Y          5
#define WHAT_XZ         6
#define WHAT_COMMAND    7
#define WHAT_EQUAL      8
#define WHAT_EOL        9
#define WHAT_STAR       10
#define WHAT_COMA       11
#define WHAT_S          13
#define WHAT_SZ         14
#define WHAT_CHAR       16
#define WHAT_LBL        17
#define lowcase(cch) tolower_tab[(unsigned char)cch]

struct slabel {
    char* name;
    long value;
    unsigned long requires;
    unsigned long conflicts;
    unsigned char ertelmes;
    unsigned char proclabel;
    unsigned char used;
#ifdef WIN32
    struct slabel *kis;
    struct slabel *nagy;
#endif
} __attribute((packed));

struct smacro {
    char *name;
    long point;
    long lin;
    char *file;
#ifdef WIN32
    struct smacro *next;
#endif
} __attribute((packed));

struct sfile {
    char *name;
    FILE *f;
    unsigned char open;
    unsigned long num;
#ifdef WIN32
    struct sfile *next;
#endif
} __attribute((packed));

struct serrorlist {
    struct serrorlist *next;
    char name;
} __attribute((packed));

struct sfilenamelist {
    struct sfilenamelist *next;
    long line;
    char name;
} __attribute((packed));

struct arguments_t {
    int warning;
    int nonlinear;
    int stripstart;
    int toascii;
    char *input;
    char *output;
    int cpumode;
    char *label;
    char *list;
    int monitor;
    int source;
    int casesensitive;
    int longbranch;
    int wordstart;
};

#ifdef _MISC_C_
extern long sline;
extern int errors,conderrors,warnings;
extern unsigned long l_address;
extern char pline[];
extern int labelexists;
extern void status();
extern unsigned long curfnum, reffile;
#endif

#ifdef _MAIN_C_
#define ignore() while(pline[lpoint]==' ') lpoint++
#define get() pline[lpoint++]
#define here() pline[lpoint]
extern char tolower_tab[256];
extern unsigned char whatis[256];
extern unsigned char petascii(unsigned char);
extern void err_msg(unsigned char, char*);
extern struct slabel* find_label(char*);
extern struct slabel* new_label(char*);
extern struct smacro* find_macro(char*);
extern struct smacro* new_macro(char*);
extern FILE* openfile(char*,char*);
extern void closefile(FILE*);
extern void tfree();
extern void labelprint();
extern void testarg(int,char **);
extern struct arguments_t arguments;
extern void freeerrorlist(int);
extern void enterfile(char *,long);
extern void exitfile();
extern struct sfilenamelist *filenamelist;
extern int encoding;
#endif

#endif
