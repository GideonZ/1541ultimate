/*
    $Id: listing.c 1513 2017-05-01 08:33:04Z soci $

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
#include "listing.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "file.h"
#include "error.h"
#include "64tass.h"
#include "opcodes.h"
#include "unicode.h"
#include "section.h"
#include "instruction.h"
#include "obj.h"
#include "mem.h"
#include "values.h"
#include "arguments.h"

#define LINE_WIDTH 8
#define ADDR_WIDTH 8
#define LADDR_WIDTH 8
#define HEX_WIDTH 16
#define MONITOR_WIDTH 16

bool listing_pccolumn;
unsigned int nolisting;   /* listing */
const uint8_t *llist = NULL;

typedef struct Listing {
    size_t c;
    unsigned int i;
    char s[LINE_WIDTH + ADDR_WIDTH + LADDR_WIDTH + 3*16];
    char hex[16];
    const uint16_t *disasm;
    const uint32_t *mnemonic;
    struct {
        size_t addr, laddr, hex, monitor, source;
    } columns;
    FILE *flist;
    uint16_t lastfile;
    const char *filename;
    unsigned int tab_size;
    bool linenum, verbose, monitor, pccolumn, source;
} Listing;

static void flushbuf(Listing *ls) {
    ls->c += fwrite(ls->s, 1, ls->i, ls->flist);
    ls->i = 0;
}

static void newline(Listing *ls) {
    putc('\n', ls->flist);
    ls->c = 0;
}

static void padding(Listing *ls, size_t t) {
    if (ls->c >= t) newline(ls);
    if (ls->tab_size > 1) {
        ls->c -= ls->c % ls->tab_size;
        while (ls->c + ls->tab_size <= t) { ls->c += ls->tab_size; putc('\t', ls->flist);} 
    }
    while (ls->c < t) { ls->c++; putc(' ', ls->flist);} 
}

static void padding2(Listing *ls, size_t t) {
    if (ls->c + ls->i >= t) {ls->s[ls->i++] = '\n'; flushbuf(ls); ls->c = 0;}
    ls->c += ls->i;
    if (ls->tab_size > 1) {
        ls->c -= ls->c % ls->tab_size;
        while (ls->c + ls->tab_size <= t) { ls->c += ls->tab_size; ls->s[ls->i++] = '\t'; } 
    }
    while (ls->c < t) { ls->c++; ls->s[ls->i++] = ' '; } 
    ls->c -= ls->i;
}

static inline void out_hex(Listing *ls, unsigned int c) {
    ls->s[ls->i++] = ls->hex[(c >> 4) & 15];
    ls->s[ls->i++] = ls->hex[c & 15];
}

static void out_byte(Listing *ls, unsigned int adr) {
    ls->s[ls->i++] = '$';
    out_hex(ls, adr);
}

static void out_word(Listing *ls, unsigned int adr) {
    out_byte(ls, adr >> 8);
    out_hex(ls, adr);
}

static void out_long(Listing *ls, unsigned int adr) {
    out_word(ls, adr >> 8);
    out_hex(ls, adr);
}

static void out_zp(Listing *ls, unsigned int adr) {
    adr = (uint16_t)(((uint8_t)adr) + dpage);
    if (adr > 0xff) out_word(ls, adr);
    else out_byte(ls, adr);
}

static void out_db(Listing *ls, unsigned int adr) {
    ls->s[ls->i++] = '$';
    if (databank != 0) out_hex(ls, databank);
    out_hex(ls, adr >> 8);
    out_hex(ls, adr);
}

static void out_pb(Listing *ls, unsigned int adr) {
    ls->s[ls->i++] = '$';
    if (current_section->l_address.bank != 0) out_hex(ls, current_section->l_address.bank >> 16);
    out_hex(ls, adr >> 8);
    out_hex(ls, adr);
}

static void out_bit(Listing *ls, unsigned int cod, unsigned int c) {
    ls->s[ls->i++] = 0x30 + ((cod >> 4) & 7); 
    ls->s[ls->i++] = ',';
    out_zp(ls, c);
}

static void out_txt(Listing *ls, const char *s) {
    fputs(s, ls->flist);
    ls->c += strlen(s);
}

MUST_CHECK Listing *listing_open(const char *filename, int argc, char *argv[]) {
    Listing *ls;
    struct linepos_s nopoint = {0, 0};
    time_t t;
    const char *prgname;
    int i;
    FILE *flist;

    flist = dash_name(filename) ? stdout : file_open(filename, "wt");
    if (flist == NULL) {
        err_msg_file(ERROR_CANT_WRTE_LST, filename, &nopoint);
        return NULL;
    }
    clearerr(flist); errno = 0;

    ls = (Listing *)mallocx(sizeof *ls);

    memcpy(ls->hex, "0123456789abcdef", 16);
    ls->filename = filename;
    ls->flist = flist;
    ls->linenum = arguments.linenum;
    ls->pccolumn = listing_pccolumn;
    ls->columns.addr = arguments.linenum ? LINE_WIDTH : 0;
    ls->columns.laddr = ls->columns.addr + ADDR_WIDTH;
    ls->columns.hex = ls->columns.laddr + (ls->pccolumn ? LADDR_WIDTH : 0);
    ls->columns.monitor = ls->columns.hex + HEX_WIDTH;
    ls->columns.source = ls->columns.monitor + (arguments.monitor ? MONITOR_WIDTH : 0);
    ls->tab_size = arguments.tab_size;
    ls->verbose = arguments.verbose;
    ls->monitor = arguments.monitor;
    ls->source = arguments.source;
    ls->lastfile = 0;
    ls->i = 0;

    fputs("\n; 64tass Turbo Assembler Macro V" VERSION " listing file\n;", flist);
    prgname = *argv;
    if (prgname != NULL) {
        const char *newp = strrchr(prgname, '/');
        if (newp != NULL) prgname = newp + 1;
#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __MSDOS__ || defined __DOS__
        newp = strrchr(prgname, '\\');
        if (newp != NULL) prgname = newp + 1;
#endif
    }
    for (i = 0; i < argc; i++) {
        putc(' ', flist);
        argv_print((i != 0) ? argv[i] : prgname, flist);
    }
    fputs("\n; ", flist);
    time(&t); fputs(ctime(&t), flist);
    newline(ls);
    if (ls->linenum) {
        out_txt(ls, ";Line");
        padding(ls, ls->columns.addr);
    }
    out_txt(ls, ";Offset");
    if (ls->pccolumn) {
        padding(ls, ls->columns.laddr);
        out_txt(ls, ";PC");
    }
    padding(ls, ls->columns.hex);
    out_txt(ls, ";Hex");
    if (ls->monitor) {
        padding(ls, ls->columns.monitor);
        out_txt(ls, ";Monitor");
    }
    if (ls->source) {
        padding(ls, ls->columns.source);
        out_txt(ls, ";Source");
    }
    newline(ls);
    return ls;
}

void listing_close(Listing *ls) {
    struct linepos_s nopoint = {0, 0};
    int err;
    if (ls == NULL) return;

    fputs("\n;******  End of listing\n", ls->flist);
    err = ferror(ls->flist);
    err |= (ls->flist != stdout) ? fclose(ls->flist) : fflush(ls->flist);
    if (err != 0 && errno != 0) err_msg_file(ERROR_CANT_WRTE_LST, ls->filename, &nopoint);
    free(ls);
}

static void printllist(Listing *ls) {
    const uint8_t *c;
    if (llist == NULL) return;
    c = llist;
    while (*c == 0x20 || *c == 0x09) c++;
    if (*c != 0) {
        padding(ls, ls->columns.source);
        printable_print(llist, ls->flist);
    }
    llist = NULL;
}

static void printline(Listing *ls) {
    int l2;
    if (curfile < 2) return;
    l2 = fprintf(ls->flist, "%" PRIuline, lpoint.line);
    if (l2 > 0) ls->c += (unsigned int)l2;
    if (ls->lastfile == curfile) return;
    l2 = fprintf(ls->flist, ":%u", (unsigned int)curfile - 1);
    if (l2 > 0) ls->c += (unsigned int)l2;
    ls->lastfile = curfile;
}

FAST_CALL void listing_equal(Listing *ls, Obj *val) {
    if (ls == NULL) return;
    if (nolisting != 0 || !ls->source || temporary_label_branch != 0) return;
    if (ls->linenum) {
        printline(ls);
        padding(ls, ls->columns.addr);
    }
    putc('=', ls->flist);
    ls->c += val_print(val, ls->flist) + 1;
    printllist(ls);
    newline(ls);
}

static void printaddr(Listing *ls, char pre, address_t addr, address_t addr2) {
    ls->s[ls->i++] = pre;
    for (;;) {
        if (addr > 0xffff) {
            if (addr >> 24 != 0) out_hex(ls, addr >> 24);
            out_hex(ls, addr >> 16);
        }
        out_hex(ls, addr >> 8);
        out_hex(ls, addr);
        if (addr2 == addr || !ls->pccolumn) return;
        addr = addr2;
        padding2(ls, ls->columns.laddr);
    }
}

static void printhex(Listing *ls, unsigned int cod, unsigned int eor, int ln, uint32_t adr) {
    padding2(ls, ls->columns.hex);
    out_hex(ls, cod ^ eor);
    while (ln > 0) {
        ls->s[ls->i++] = ' '; 
        out_hex(ls, adr ^ eor); 
        adr >>= 8;
        ln--;
    }
}

static void printhex2(Listing *ls, unsigned int ln, const uint8_t *data) {
    unsigned int i = 0;
    padding2(ls, ls->columns.hex);
    for (;;) {
        out_hex(ls, data[i++]);
        if (i >= ln) break;
        ls->s[ls->i++] = ' '; 
    }
}

static void printmon(Listing *ls, unsigned int cod, int ln, uint32_t adr) {
    const char *mode;
    Adr_types type;
    uint32_t mnem;

    padding2(ls, ls->columns.monitor);
    mnem = ls->mnemonic[ls->disasm[cod] & 0xff];
    ls->s[ls->i++] = (char)(mnem >> 16);
    ls->s[ls->i++] = (char)(mnem >> 8);
    ls->s[ls->i++] = (char)(mnem);
    ls->s[ls->i++] = ' ';

    type = (Adr_types)(ls->disasm[cod] >> 8);
    mode = addr_modes[type];
    if (*mode != ' ') ls->s[ls->i++] = *mode;
    mode++;
    switch (type) {
    case ADR_IMPLIED: ls->i--; return;
    case ADR_REG: return;
    case ADR_IMMEDIATE:
        switch (ln) {
        default: ls->i -= 2; return;
        case 0: return;
        case 1: out_byte(ls, adr); return;
        case 2: out_word(ls, adr); return;
        }
    case ADR_ADDR: if (cod == 0x20 || cod == 0x4c) out_pb(ls, adr); else out_db(ls, adr); return;
    case ADR_BIT_ZP: out_bit(ls, cod, adr); return;
    case ADR_LONG:
    case ADR_LONG_X: out_long(ls, adr); break;
    case ADR_ADDR_X_I: out_pb(ls, adr); break;
    case ADR_ZP_R:
    case ADR_ZP_R_I_Y:
    case ADR_ZP_S:
    case ADR_ZP_S_I_Y: out_byte(ls, adr); break;
    case ADR_ADDR_X:
    case ADR_ADDR_Y: out_db(ls, adr); break;
    case ADR_ZP:
    case ADR_ZP_I:
    case ADR_ZP_I_Y:
    case ADR_ZP_I_Z:
    case ADR_ZP_LI:
    case ADR_ZP_LI_Y:
    case ADR_ZP_X:
    case ADR_ZP_X_I:
    case ADR_ZP_Y: out_zp(ls, adr); break;
    case ADR_ADDR_I:
    case ADR_ADDR_LI: out_word(ls, adr); break;
    case ADR_REL: if (ln > 0) out_pb(ls, (address_t)((int8_t)adr + (int)current_section->l_address.address)); else ls->i--; return;
    case ADR_BIT_ZP_REL: 
        out_bit(ls, cod, adr);
        ls->s[ls->i++] = ',';
        out_pb(ls, (address_t)((int8_t)(adr >> 8) + (int)current_section->l_address.address));
        return;
    case ADR_REL_L: if (ln > 0) out_pb(ls, adr + (((cod & 0x0F) == 3) ? -1u : 0) + current_section->l_address.address); else ls->i--; return;
    case ADR_MOVE: out_byte(ls, adr >> 8); ls->s[ls->i++] = ','; out_byte(ls, adr); return;
    case ADR_LEN: return;/* not an addressing mode */
    }
    while (*mode != 0) ls->s[ls->i++] = *mode++;
}

static void printsource(Listing *ls, linecpos_t pos) {
    while (llist[pos-1] == 0x20 || llist[pos-1] == 0x09) pos--;
    padding(ls, ls->columns.source);
    printable_print2(llist, ls->flist, pos);
    newline(ls);
}

void FAST_CALL listing_line(Listing *ls, linecpos_t pos) {
    size_t i;
    if (nolisting != 0  || temporary_label_branch != 0 || llist == NULL) return;
    if (ls == NULL) {
        address_t addr;
        if (!fixeddig || constcreated || listing_pccolumn || !arguments.source) return;
        addr = (current_section->l_address.address & 0xffff) | current_section->l_address.bank;
        i = 0;
        while (i < pos && (llist[i] == 0x20 || llist[i] == 0x09)) i++;
        if (i < pos && current_section->address != addr) listing_pccolumn = true;
        return;
    }
    if (!ls->source) return;
    i = 0;
    while (i < pos && (llist[i] == 0x20 || llist[i] == 0x09)) i++;
    if (i < pos) {
        if (ls->linenum) {
            printline(ls);
            padding2(ls, ls->columns.addr);
        }
        printaddr(ls, '.', current_section->address, (current_section->l_address.address & 0xffff) | current_section->l_address.bank);
        flushbuf(ls);
    }
    if (ls->verbose) {
        if (llist[i] != 0) {
            if (ls->c == 0 && ls->linenum) printline(ls);
            padding(ls, ls->columns.source);
            printable_print(llist, ls->flist);
        }
        newline(ls);
    } else {
        if (ls->c != 0) printsource(ls, pos);
    }
    llist = NULL;
}

void FAST_CALL listing_line_cut(Listing *ls, linecpos_t pos) {
    size_t i;
    if (nolisting != 0 || temporary_label_branch != 0 || llist == NULL) return;
    if (ls == NULL) {
        address_t addr;
        if (!fixeddig || constcreated || listing_pccolumn || !arguments.source) return;
        addr = (current_section->l_address.address & 0xffff) | current_section->l_address.bank;
        i = 0;
        while (i < pos && (llist[i] == 0x20 || llist[i] == 0x09)) i++;
        if (i < pos && current_section->address != addr) listing_pccolumn = true;
        return;
    }
    if (!ls->source) return;
    i = 0;
    while (i < pos && (llist[i] == 0x20 || llist[i] == 0x09)) i++;
    if (i < pos) {
        if (ls->linenum) {
            printline(ls);
            padding2(ls, ls->columns.addr);
        }
        printaddr(ls, '.', current_section->address, (current_section->l_address.address & 0xffff) | current_section->l_address.bank);
        flushbuf(ls);
        printsource(ls, pos);
    }
    llist = NULL;
}

void FAST_CALL listing_line_cut2(Listing *ls, linecpos_t pos) {
    if (ls == NULL || !ls->verbose || llist == NULL) return;
    if (nolisting == 0 && ls->source && temporary_label_branch == 0) {
        if (ls->linenum) printline(ls);
        padding(ls, ls->columns.source);
        caret_print(llist, ls->flist, pos);
        printable_print(llist + pos, ls->flist);
        newline(ls);
        llist = NULL;
    }
}

FAST_CALL void listing_set_cpumode(Listing *ls, const struct cpu_s *cpumode) {
    if (ls == NULL) return;
    ls->disasm = cpumode->disasm;
    ls->mnemonic = cpumode->mnemonic; 
}

void listing_instr(Listing *ls, uint8_t cod, uint32_t adr, int ln) {
    address_t addr, addr2;
    if (nolisting != 0 || temporary_label_branch != 0) return;
    if (ls == NULL) {
        if (!fixeddig || constcreated || listing_pccolumn) return;
        addr = (((int)current_section->l_address.address - ln - 1) & 0xffff) | current_section->l_address.bank;
        addr2 = (address_t)((int)current_section->address - ln - 1) & all_mem2;
        if (addr2 != addr) listing_pccolumn = true;
        return;
    }
    if (ls->linenum) {
        if (llist != NULL) printline(ls);
        padding2(ls, ls->columns.addr);
    }
    addr = (((int)current_section->l_address.address - ln - 1) & 0xffff) | current_section->l_address.bank;
    addr2 = (address_t)((int)current_section->address - ln - 1) & all_mem2;
    printaddr(ls, '.', addr2, addr);
    if (current_section->dooutput && ln >= 0) {
        printhex(ls, cod, outputeor, ln, adr);
        if (ls->monitor) {
            printmon(ls, cod, ln, adr);
        }
    }
    flushbuf(ls);
    if (ls->source) printllist(ls);
    newline(ls);
}

void listing_mem(Listing *ls, const uint8_t *data, size_t len, address_t myaddr, address_t myaddr2) { 
    bool print, exitnow;
    int lcol;
    unsigned int repeat;
    struct {
        uint8_t data[16];
        unsigned int len;
        address_t addr, addr2;
    } prev, current;
    size_t p;

    if (nolisting != 0 || temporary_label_branch != 0) return;
    if (ls == NULL) {
         if (myaddr != myaddr2) listing_pccolumn = true;
         return;
    }
    print = true; exitnow = false;
    prev.addr = current.addr = myaddr;
    prev.addr2 = current.addr2 = myaddr2;
    repeat = 0; p = 0; prev.len = current.len = 0;
    if (len != 0) {
        lcol = ls->source ? (ls->monitor ? 8 : 4) : 16;
        while (len != 0) {
            if ((lcol--) == 0) {
                if (print || prev.len != current.len || memcmp(prev.data, current.data, current.len) != 0 || ls->verbose) {
                flush:
                    if (repeat != 0) {
                        if (ls->linenum) padding2(ls, ls->columns.addr);
                        if (repeat == 1) {
                            printaddr(ls, '>', prev.addr, prev.addr2);
                            printhex2(ls, prev.len, prev.data);
                            flushbuf(ls);
                        } else {
                            ls->s[ls->i++] = ';';
                            padding2(ls, ls->columns.hex);
                            flushbuf(ls);
                            fprintf(ls->flist, "...repeated %u times (%u bytes)...", repeat, repeat * 16);
                        }
                        newline(ls);
                        repeat = 0;
                    }
                    if (ls->linenum) {
                        if (print) printline(ls);
                        padding2(ls, ls->columns.addr);
                    }
                    printaddr(ls, '>', current.addr, current.addr2);
                    if (current.len != 0) {
                        printhex2(ls, current.len, current.data);
                    }
                    flushbuf(ls);
                    if (ls->source && print) printllist(ls);
                    newline(ls);
                    if (exitnow) return;
                    memcpy(&prev, &current, sizeof prev);
                    print = false;
                } else {
                    repeat++;
                    prev.addr = current.addr;
                    prev.addr2 = current.addr2;
                }
                current.len = 0;
                current.addr = myaddr;
                current.addr2 = myaddr2;
                lcol = 15;
            }
            current.data[current.len++] = data[p++];
            myaddr = (myaddr + 1) & all_mem2;
            myaddr2 = ((myaddr2 + 1) & 0xffff) | (myaddr2 & ~(address_t)0xffff);
            len--;
        }
    }
    exitnow = true;
    goto flush;
}

void listing_file(Listing *ls, const char *txt, const char *name) {
    if (ls == NULL) return;
    newline(ls);
    if (ls->linenum) {
        int l = (name != NULL) ? fprintf(ls->flist, ":%u", (unsigned int)curfile - 1) : 0;
        if (l > 0) ls->c += (unsigned int)l;
        padding(ls, ls->columns.addr);
        ls->lastfile = curfile;
    };
    fputs(txt, ls->flist);
    if (name != NULL) argv_print(name, ls->flist);
    newline(ls);
    newline(ls);
}
