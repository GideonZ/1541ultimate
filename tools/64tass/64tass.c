/*
    Turbo Assembler 6502/65C02/65816/DTV
    $Id: 64tass.c 1512 2017-05-01 08:19:51Z soci $

    6502/65C02 Turbo Assembler  Version 1.3
    (c) 1996 Taboo Productions, Marek Matula

    6502/65C02 Turbo Assembler  Version 1.35  ANSI C port
    (c) 2000 BiGFooT/BReeZe^2000

    6502/65C02/65816/DTV Turbo Assembler  Version 1.4x
    (c) 2001-2014 Soci/Singular (soci@c64.rulez.org)

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

#include "64tass.h"
#ifdef _WIN32
#include <windows.h>
#include <wincon.h>
#endif
#include <locale.h>
#include "wchar.h"
#include <string.h>

#include "error.h"
#include "opcodes.h"
#include "eval.h"
#include "values.h"
#include "section.h"
#include "encoding.h"
#include "file.h"
#include "variables.h"
#include "macro.h"
#include "instruction.h"
#include "unicode.h"
#include "listing.h"
#include "optimizer.h"
#include "arguments.h"
#include "ternary.h"
#include "opt_bit.h"
#include "longjump.h"

#include "listobj.h"
#include "codeobj.h"
#include "strobj.h"
#include "floatobj.h"
#include "addressobj.h"
#include "boolobj.h"
#include "bytesobj.h"
#include "intobj.h"
#include "bitsobj.h"
#include "functionobj.h"
#include "namespaceobj.h"
#include "operobj.h"
#include "gapobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "registerobj.h"
#include "labelobj.h"
#include "errorobj.h"
#include "macroobj.h"
#include "mfuncobj.h"

struct Listing *listing = NULL;
int temporary_label_branch; /* function declaration in function context, not good */
linepos_t poke_pos;
line_t vline;      /* current line */
address_t all_mem, all_mem2;
unsigned int all_mem_bits;
uint8_t pass = 0, max_pass = MAX_PASS;         /* pass */
address_t star = 0;
const uint8_t *pline;           /* current line data */
struct linepos_s lpoint;        /* position in current line */
static uint8_t strength = 0;
bool fixeddig, constcreated;
unsigned int outputeor = 0; /* EOR value for final output (usually 0, except changed by .eor) */
bool referenceit = true;
const struct cpu_s *cpu;

static size_t waitfor_p, waitfor_len;
static struct waitfor_s {
    Wait_types what;
    struct linepos_s epoint;
    address_t addr, addr2;
    address2_t laddr, laddr2;
    Label *label;
    size_t memp, membp;
    struct section_s *section;
    Obj *val;
    uint8_t skip;
    bool breakout;
} *waitfors, *waitfor, *prevwaitfor;

uint16_t reffile, curfile;
uint32_t backr, forwr;
struct avltree *star_tree = NULL;

static const char * const command[] = { /* must be sorted, first char is the ID */
    "\x08" "addr",
    "\x22" "al",
    "\x34" "align",
    "\x21" "as",
    "\x35" "assert",
    "\x5b" "autsiz",
    "\x3a" "bend",
    "\x1a" "binary",
    "\x50" "binclude",
    "\x39" "block",
    "\x5a" "break",
    "\x05" "byte",
    "\x54" "case",
    "\x4e" "cdef",
    "\x32" "cerror",
    "\x06" "char",
    "\x36" "check",
    "\x1b" "comment",
    "\x59" "continue",
    "\x37" "cpu",
    "\x33" "cwarn",
    "\x28" "databank",
    "\x55" "default",
    "\x0d" "dint",
    "\x29" "dpage",
    "\x4c" "dsection",
    "\x47" "dstruct",
    "\x4a" "dunion",
    "\x0e" "dword",
    "\x4f" "edef",
    "\x15" "else",
    "\x17" "elsif",
    "\x2c" "enc",
    "\x3f" "end",
    "\x1c" "endc",
    "\x52" "endf",
    "\x2d" "endif",
    "\x11" "endm",
    "\x1e" "endp",
    "\x46" "ends",
    "\x56" "endswitch",
    "\x49" "endu",
    "\x58" "endweak",
    "\x40" "eor",
    "\x25" "error",
    "\x16" "fi",
    "\x2a" "fill",
    "\x12" "for",
    "\x51" "function",
    "\x44" "goto",
    "\x20" "here",
    "\x3e" "hidemac",
    "\x14" "if",
    "\x2f" "ifeq",
    "\x31" "ifmi",
    "\x2e" "ifne",
    "\x30" "ifpl",
    "\x19" "include",
    "\x43" "lbl",
    "\x0b" "lint",
    "\x1f" "logical",
    "\x0c" "long",
    "\x10" "macro",
    "\x5c" "mansiz",
    "\x13" "next",
    "\x04" "null",
    "\x0f" "offs",
    "\x38" "option",
    "\x1d" "page",
    "\x27" "pend",
    "\x26" "proc",
    "\x3c" "proff",
    "\x3b" "pron",
    "\x01" "ptext",
    "\x18" "rept",
    "\x07" "rta",
    "\x4b" "section",
    "\x5d" "seed",
    "\x41" "segment",
    "\x4d" "send",
    "\x02" "shift",
    "\x03" "shiftl",
    "\x3d" "showmac",
    "\x09" "sint",
    "\x45" "struct",
    "\x53" "switch",
    "\x00" "text",
    "\x48" "union",
    "\x42" "var",
    "\x2b" "warn",
    "\x57" "weak",
    "\x0a" "word",
    "\x24" "xl",
    "\x23" "xs",
};

typedef enum Command_types {
    CMD_TEXT = 0, CMD_PTEXT, CMD_SHIFT, CMD_SHIFTL, CMD_NULL, CMD_BYTE, CMD_CHAR,
    CMD_RTA, CMD_ADDR, CMD_SINT, CMD_WORD, CMD_LINT, CMD_LONG, CMD_DINT,
    CMD_DWORD, CMD_OFFS, CMD_MACRO, CMD_ENDM, CMD_FOR, CMD_NEXT, CMD_IF,
    CMD_ELSE, CMD_FI, CMD_ELSIF, CMD_REPT, CMD_INCLUDE, CMD_BINARY,
    CMD_COMMENT, CMD_ENDC, CMD_PAGE, CMD_ENDP, CMD_LOGICAL, CMD_HERE, CMD_AS,
    CMD_AL, CMD_XS, CMD_XL, CMD_ERROR, CMD_PROC, CMD_PEND, CMD_DATABANK,
    CMD_DPAGE, CMD_FILL, CMD_WARN, CMD_ENC, CMD_ENDIF, CMD_IFNE, CMD_IFEQ,
    CMD_IFPL, CMD_IFMI, CMD_CERROR, CMD_CWARN, CMD_ALIGN, CMD_ASSERT,
    CMD_CHECK, CMD_CPU, CMD_OPTION, CMD_BLOCK, CMD_BEND, CMD_PRON, CMD_PROFF,
    CMD_SHOWMAC, CMD_HIDEMAC, CMD_END, CMD_EOR, CMD_SEGMENT, CMD_VAR, CMD_LBL,
    CMD_GOTO, CMD_STRUCT, CMD_ENDS, CMD_DSTRUCT, CMD_UNION, CMD_ENDU,
    CMD_DUNION, CMD_SECTION, CMD_DSECTION, CMD_SEND, CMD_CDEF, CMD_EDEF,
    CMD_BINCLUDE, CMD_FUNCTION, CMD_ENDF, CMD_SWITCH, CMD_CASE, CMD_DEFAULT,
    CMD_ENDSWITCH, CMD_WEAK, CMD_ENDWEAK, CMD_CONTINUE, CMD_BREAK, CMD_AUTSIZ,
    CMD_MANSIZ, CMD_SEED
} Command_types;

/* --------------------------------------------------------------------------- */
static void tfree(void) {
    destroy_lastlb();
    destroy_eval();
    destroy_variables();
    destroy_section();
    destroy_longjump();
    destroy_file();
    err_destroy();
    destroy_encoding();
    destroy_values();
    destroy_namespacekeys();
    destroy_ternary();
    destroy_opt_bit();
    free(arguments.symbol_output);
    unfc(NULL);
    unfkc(NULL, NULL, 0);
    str_cfcpy(NULL, NULL);
}

static void status(void) {
    bool errors = error_print();
    if (arguments.quiet) {
        error_status();
        printf("Passes: %12u\n",pass);
        if (!errors) sectionprint();
    }
    tfree();
    free_macro();
    free(waitfors);
}

void new_waitfor(Wait_types what, linepos_t epoint) {
    waitfor_p++;
    if (waitfor_p >= waitfor_len) {
        waitfor_len += 8;
        if (/*waitfor_len < 8 ||*/ waitfor_len > SIZE_MAX / sizeof *waitfors) err_msg_out_of_memory(); /* overflow */
        waitfors = (struct waitfor_s *)reallocx(waitfors, waitfor_len * sizeof *waitfors);
    }
    waitfor = &waitfors[waitfor_p];
    prevwaitfor = (waitfor_p != 0) ? &waitfors[waitfor_p - 1] : waitfor;
    waitfor->what = what;
    waitfor->epoint = *epoint;
    waitfor->label = NULL;
    waitfor->val = NULL;
    waitfor->skip = prevwaitfor->skip;
}

static void reset_waitfor(void) {
    struct linepos_s lpos = {0, 0};
    waitfor_p = (size_t)-1;
    new_waitfor(W_NONE, &lpos);
    waitfor->skip = 1;
    prevwaitfor = waitfor;
}

static bool close_waitfor(Wait_types what) {
    if (waitfor->what == what) {
        if (waitfor->val != NULL) val_destroy(waitfor->val);
        waitfor_p--;
        waitfor = &waitfors[waitfor_p];
        prevwaitfor = (waitfor_p != 0) ? &waitfors[waitfor_p - 1] : waitfor;
        return true;
    }
    return false;
}

static void set_size(const Label *label, address_t size, struct memblocks_s *mem, size_t memp, size_t membp) {
    Code *code = (Code *)label->value;
    size &= all_mem2;
    if (code->size != size) {
        code->size = size;
        if (code->pass != 0) {
            if (fixeddig && pass > max_pass) err_msg_cant_calculate(&label->name, &label->epoint);
            fixeddig = false;
        }
    }
    code->pass = pass;
    code->mem = mem;
    code->memp = memp;
    code->membp = membp;
}

static bool tobool(const struct values_s *v1, bool *truth) {
    Obj *val = v1->val, *err;
    Type *obj = val->obj;
    bool error;
    if (obj == BOOL_OBJ) {
        *truth = val == &true_value->v;
        return false;
    }
    err = obj->truth(val, TRUTH_BOOL, &v1->epoint);
    error = err->obj != BOOL_OBJ;
    if (error) {
        if (err->obj == ERROR_OBJ) {
            err_msg_output((Error *)err);
        }
    } else {
        *truth = (Bool *)err == true_value;
        if (diagnostics.strict_bool) err_msg_bool(ERROR_____CANT_BOOL, val, &v1->epoint);
    }
    val_destroy(err);
    return error;
}

static bool tostr(const struct values_s *v1, str_t *out) {
    Obj *val = v1->val;
    switch (val->obj->type) {
    case T_STR:
        out->len = ((Str *)val)->len;
        out->data = ((Str *)val)->data;
        return false;
    case T_NONE:
        err_msg_still_none(NULL, &v1->epoint);
        return true;
    case T_ERROR:
        err_msg_output((Error *)val);
        return true;
    default:
        err_msg_wrong_type(val, STR_OBJ, &v1->epoint);
        return true;
    }
}

static MUST_CHECK bool touval2(Obj *v1, uval_t *uv, unsigned int bits, linepos_t epoint) {
    Error *err = v1->obj->uval2(v1, uv, bits, epoint);
    if (err == NULL) return false;
    err_msg_output_and_destroy(err);
    return true;
}

/* --------------------------------------------------------------------------- */
/*
 * Skip memory
 */
static void memskip(address_t db) { /* poke_pos! */
    if (current_section->moved) {
        if (current_section->address < current_section->start) err_msg2(ERROR_OUTOF_SECTION, NULL, poke_pos);
        if (current_section->wrapwarn) {err_msg_mem_wrap(poke_pos);current_section->wrapwarn = false;}
        current_section->moved = false;
    }
    if (current_section->l_address.address > 0xffff || db > 0x10000 - current_section->l_address.address) {
        current_section->l_address.address = ((current_section->l_address.address + db - 1) & 0xffff) + 1;
        err_msg_pc_wrap(poke_pos);
    } else current_section->l_address.address += db;
    if (db > (~current_section->address & all_mem2)) {
        if (db - 1 + current_section->address == all_mem2) {
            current_section->wrapwarn = current_section->moved = true;
            if (current_section->end <= all_mem2) current_section->end = all_mem2 + 1;
            current_section->address = 0;
        } else {
            if (current_section->start != 0) err_msg2(ERROR_OUTOF_SECTION, NULL, poke_pos);
            if (current_section->end <= all_mem2) current_section->end = all_mem2 + 1;
            current_section->moved = false;
            current_section->address = (current_section->address + db) & all_mem2;
            err_msg_mem_wrap(poke_pos);current_section->wrapwarn = false;
        }
    } else current_section->address += db;
    memjmp(&current_section->mem, current_section->address);
}

/* --------------------------------------------------------------------------- */
/*
 * output one byte
 */
FAST_CALL void pokeb(unsigned int byte) { /* poke_pos! */
    if (current_section->moved) {
        if (current_section->address < current_section->start) err_msg2(ERROR_OUTOF_SECTION, NULL, poke_pos);
        if (current_section->wrapwarn) {err_msg_mem_wrap(poke_pos);current_section->wrapwarn = false;}
        current_section->moved = false;
    }
    if (current_section->l_address.address > 0xffff) {
        current_section->l_address.address = 0;
        err_msg_pc_wrap(poke_pos);
    }
    if (current_section->dooutput) write_mem(&current_section->mem, byte ^ outputeor);
    current_section->address++;current_section->l_address.address++;
    if ((current_section->address & ~all_mem2) != 0 || current_section->address == 0) {
        current_section->wrapwarn = current_section->moved = true;
        if (current_section->end <= all_mem2) current_section->end = all_mem2 + 1;
        current_section->address = 0;
        memjmp(&current_section->mem, current_section->address);
    }
}

/* --------------------------------------------------------------------------- */
static int get_command(void) {
    unsigned int no, also = 0, felso, elozo;
    const uint8_t *label;
    size_t l;
    int s4;
    lpoint.pos++;
    label = pline + lpoint.pos;
    l = get_label();
    if (l != 0 && l < 19) {
        uint8_t cmd[20];
        if (arguments.caseinsensitive != 0) {
            size_t i;
            for (i = 0; i < l; i++) cmd[i] = label[i] | 0x20;
        } else memcpy(cmd, label, l);
        cmd[l] = 0;
        if (l != 0) {
            felso = sizeof(command)/sizeof(command[0]);
            no = felso/2;
            for (;;) {  /* do binary search */
                if ((s4=strcmp((const char *)cmd, command[no] + 1)) == 0) {
                    return (uint8_t)command[no][0];
                }

                elozo = no;
                no = ((s4 > 0) ? (felso + (also = no)) : (also + (felso = no)))/2;
                if (elozo == no) break;
            }
        }
    }
    lpoint.pos -= l;
    return lenof(command);
}

/* ------------------------------------------------------------------------------ */

static void set_cpumode(const struct cpu_s *cpumode) {
    cpu = cpumode;
    all_mem = cpumode->max_address;
    all_mem_bits = (all_mem == 0xffff) ? 16 : 24;
    select_opcodes(cpumode);
    listing_set_cpumode(listing, cpumode);
    cpu_opt_set_cpumode(cpumode);
    if (registerobj_createnames(cpumode->registers)) constcreated = true;
}

static void const_assign(Label *label, Obj *val) {
    label->defpass = pass;
    if (fixeddig && label->usepass >= pass) {
        if (val->obj->same(val, label->value)) {
            val_destroy(val);
            return;
        }
        if (pass > max_pass) err_msg_cant_calculate(&label->name, &label->epoint);
        fixeddig = false;
    }
    val_destroy(label->value);
    label->value = val;
}

static bool textrecursion(Obj *val, int prm, int *ch2, address_t *uninit, address_t *sum, size_t max) {
    Iter *iter;
    iter_next_t iter_next;
    Obj *val2 = NULL;
    uval_t uval;
    bool warn = false;

    if (*sum >= max) return false;
retry:
    switch (val->obj->type) {
    case T_STR:
        {
            Obj *tmp;
            Textconv_types m;
            switch (prm) {
            case CMD_SHIFTL:
            case CMD_SHIFT: m = BYTES_MODE_SHIFT_CHECK; break;
            case CMD_NULL: m = BYTES_MODE_NULL_CHECK; break;
            default: m = BYTES_MODE_TEXT; break;
            }
            tmp = bytes_from_str((Str *)val, poke_pos, m);
            iter = tmp->obj->getiter(tmp);
            val_destroy(tmp);
            break;
        }
    case T_FLOAT:
    case T_INT:
    case T_BOOL:
        iter = NULL;
        val2 = val;
        goto doit;
    case T_CODE:
        val = ((Code *)val)->addr;
        goto retry;
    case T_GAP:
        iter = NULL;
        goto dogap;
    case T_BITS:
        {
            Obj *tmp;
            size_t bits = ((Bits *)val)->bits;
            if (bits == 0) return false;
            if (bits <= 8) {
                iter = NULL;
                val2 = val;
                goto doit;
            }
            tmp = BYTES_OBJ->create(val, poke_pos);
            iter = tmp->obj->getiter(tmp);
            val_destroy(tmp);
            break;
        }
    case T_ERROR:
        err_msg_output((Error *)val);
        if (*ch2 < -1) *ch2 = -1;
        return false;
    case T_NONE:
        return true;
    case T_BYTES:
        {
            ssize_t len = ((Bytes *)val)->len;
            if (len < 0) len = ~len;
            if (len == 0) return false;
            if (len == 1) {
                iter = NULL;
                val2 = val;
                goto doit;
            }
        }
        /* fall through */
    default:
        iter = val->obj->getiter(val);
    }

    iter_next = iter->v.obj->next;
    while ((val2 = iter_next(iter)) != NULL) {
        switch (val2->obj->type) {
        case T_BITS:
            {
                size_t bits = ((Bits *)val2)->bits;
                if (bits == 0) break;
                if (bits <= 8) goto doit;
            }
            /* fall through */
        case T_LIST:
        case T_TUPLE:
        case T_STR:
        rec:
            if (textrecursion(val2, prm, ch2, uninit, sum, max)) warn = true;
            break;
        case T_GAP:
        dogap:
            if (*ch2 >= 0) {
                if (*uninit != 0) { memskip(*uninit); (*sum) += *uninit; *uninit = 0; }
                pokeb((unsigned int)*ch2); (*sum)++;
            }
            *ch2 = -1; (*uninit)++;
            if (iter == NULL) return warn;
            break;
        case T_BYTES:
            {
                ssize_t len = ((Bytes *)val2)->len;
                if (len < 0) len = ~len;
                if (len == 0) break;
                if (len > 1) goto rec;
            }
            /* fall through */
        default:
        doit:
            if (*ch2 >= 0) {
                if (*uninit != 0) { memskip(*uninit); (*sum) += *uninit; *uninit = 0; }
                pokeb((unsigned int)*ch2); (*sum)++;
            }
            if (touval(val2, &uval, 8, poke_pos)) uval = 256 + '?';
            switch (prm) {
            case CMD_SHIFT:
                if ((uval & 0x80) != 0) err_msg2(ERROR___NO_HIGH_BIT, NULL, poke_pos);
                *ch2 = uval & 0x7f;
                break;
            case CMD_SHIFTL:
                if ((uval & 0x80) != 0) err_msg2(ERROR___NO_HIGH_BIT, NULL, poke_pos);
                *ch2 = (uval << 1) & 0xfe;
                break;
            case CMD_NULL:
                if (uval == 0) err_msg2(ERROR_NO_ZERO_VALUE, NULL, poke_pos);
                /* fall through */
            default:
                *ch2 = uval & 0xff;
                break;
            }
            if (iter == NULL) return warn;
            break;
        case T_NONE:
            warn = true;
        }
        val_destroy(val2);
        if (*sum >= max) break;
    }
    val_destroy(&iter->v);
    return warn;
}

static bool byterecursion(Obj *val, int prm, address_t *uninit, int bits) {
    Iter *iter;
    iter_next_t iter_next;
    Obj *val2;
    uint32_t ch2;
    uval_t uv;
    ival_t iv;
    bool warn = false;
    Type *type = val->obj;
    if (type != LIST_OBJ && type != TUPLE_OBJ) {
        if (type == GAP_OBJ) {
            *uninit += (unsigned int)abs(bits) / 8;
            return false;
        }
        iter = NULL;
        if (type == NONE_OBJ) goto donone;
        val2 = val;
        goto doit;
    }
    iter = type->getiter(val);
    iter_next = iter->v.obj->next;
    while ((val2 = iter_next(iter)) != NULL) {
        switch (val2->obj->type) {
        case T_LIST:
        case T_TUPLE:
            if (byterecursion(val2, prm, uninit, bits)) warn = true;
            val_destroy(val2);
            continue;
        case T_GAP:
            *uninit += (unsigned int)abs(bits) / 8; val_destroy(val2);
            continue;
        default:
        doit:
            if (prm == CMD_RTA || prm == CMD_ADDR) {
                atype_t am;
                Obj *tmp = val2->obj->address(val2, &am);
                if (touval(tmp, &uv, (am == A_KR) ? 16 : all_mem_bits, poke_pos)) {
                    ch2 = 0;
                    break;
                }
                uv &= all_mem;
                switch (am) {
                case A_NONE:
                    if ((current_section->l_address.bank ^ uv) > 0xffff) err_msg2(ERROR_CANT_CROSS_BA, tmp, poke_pos);
                    break;
                case A_KR:
                    break;
                default:
                    err_msg_output_and_destroy(err_addressing(am, poke_pos));
                }
                ch2 = (prm == CMD_RTA) ? (uv - 1) : uv;
                break;
            }
            if (bits >= 0) {
                if (touval(val2, &uv, (unsigned int)bits, poke_pos)) {
                    if (diagnostics.pitfalls) {
                        static unsigned int once;
                        if (prm == CMD_BYTE && val2->obj == STR_OBJ) err_msg_byte_note(poke_pos);
                        else if (prm != CMD_RTA && prm != CMD_ADDR && once != pass) {
                            Error *err = val2->obj->ival(val2, &iv, (unsigned int)bits, poke_pos);
                            if (err != NULL) val_destroy(&err->v);
                            else {
                                const char *txt;
                                switch (prm) {
                                case CMD_BYTE:  txt = ".char"; break;
                                case CMD_LONG:  txt = ".lint"; break;
                                case CMD_DWORD: txt = ".dint"; break;
                                default:
                                case CMD_WORD:  txt = ".sint"; break;
                                }
                                err_msg_char_note(txt, poke_pos);
                                once = pass;
                            }
                        }
                    }
                    uv = 0;
                }
                ch2 = uv;
            } else {
                if (toival(val2, &iv, (unsigned int)-bits, poke_pos)) iv = 0;
                ch2 = (uint32_t)iv;
            }
            break;
        case T_NONE:
        donone:
            warn = true;
            ch2 = 0;
        }
        if (*uninit != 0) {memskip(*uninit);*uninit = 0;}
        pokeb(ch2);
        if (prm>=CMD_RTA) pokeb(ch2 >> 8);
        if (prm>=CMD_LINT) pokeb(ch2 >> 16);
        if (prm>=CMD_DINT) pokeb(ch2 >> 24);
        if (iter == NULL) return warn;
        val_destroy(val2);
    }
    val_destroy(&iter->v);
    return warn;
}

static bool instrecursion(List *val, int prm, unsigned int w, linepos_t epoint, struct linepos_s *epoints) {
    size_t i;
    Error *err;
    bool was = false;
    for (i = 0; i < val->len; i++) {
        Obj *tmp = val->data[i];
        if (tmp->obj == TUPLE_OBJ || tmp->obj == LIST_OBJ) {
            if (instrecursion((List *)tmp, prm, w, epoint, epoints)) was = true;
            continue;
        }
        err = instruction(prm, w, tmp, epoint, epoints);
        if (err != NULL) err_msg_output_and_destroy(err); else was = true;
    }
    return was;
}

static void starhandle(Obj *val, linepos_t epoint, linepos_t epoint2) {
    uval_t uval;
    atype_t am;
    address_t addr, laddr;

    current_section->wrapwarn = false;
    if (!current_section->moved) {
        if (current_section->end < current_section->address) current_section->end = current_section->address;
        current_section->moved = true;
    }
    listing_line(listing, epoint->pos);
    do {
        if (current_section->structrecursion != 0 && !current_section->dooutput) {
            err_msg2(ERROR___NOT_ALLOWED, "*=", epoint);
            break;
        }
        {
            address_t max = (current_section->logicalrecursion == 0) ? all_mem2 : all_mem;
            if (touval(val->obj->address(val, &am), &uval, (max == 0xffff) ? 16 : (max == 0xffffff) ? 24 : 32, epoint2)) {
                break;
            }
        }
        if (am != A_NONE && check_addr(am)) {
            err_msg_output_and_destroy(err_addressing(am, epoint2));
            break;
        }
        if (current_section->logicalrecursion == 0) {
            current_section->l_address.address = uval & 0xffff;
            current_section->l_address.bank = uval & all_mem & ~(address_t)0xffff;
            val_destroy(current_section->l_address_val);
            current_section->l_address_val = val;
            addr = (address_t)uval & all_mem2;
            if (current_section->address != addr) {
                current_section->address = addr;
                memjmp(&current_section->mem, current_section->address);
            }
            return;
        }
        laddr = (current_section->l_address.address + current_section->l_address.bank) & all_mem; /* overflow included! */
        addr = (address_t)uval & all_mem;
        if (arguments.tasmcomp) addr = (uint16_t)addr;
        else if (addr >= laddr) {
            addr = (current_section->address + (addr - laddr)) & all_mem2;
        } else {
            addr = (current_section->address - (laddr - addr)) & all_mem2;
        }
        if (current_section->address != addr) {
            current_section->address = addr;
            memjmp(&current_section->mem, current_section->address);
        }
        current_section->l_address.address = uval & 0xffff;
        current_section->l_address.bank = uval & all_mem & ~(address_t)0xffff;
        val_destroy(current_section->l_address_val);
        current_section->l_address_val = val;
        return;
    } while (false);
    val_destroy(val);
}

static MUST_CHECK Oper *oper_from_token(int wht) {
    switch (wht) {
    case 'X':
        if (arguments.caseinsensitive == 0) {
            return NULL;
        }
        /* fall through */
    case 'x': return &o_X;
    case '*': return &o_MUL;
    case '+': return &o_ADD;
    case '-': return &o_SUB;
    case '/': return &o_DIV;
    case '%': return &o_MOD;
    case '|': return &o_OR;
    case '&': return &o_AND;
    case '^': return &o_XOR;
    case '.': return &o_MEMBER;
    default: return NULL;
    }
}

static MUST_CHECK Oper *oper_from_token2(int wht, int wht2) {
    if (wht == wht2) {
        switch (wht) {
        case '&': return &o_LAND;
        case '|': return &o_LOR;
        case '>': return &o_RSHIFT;
        case '<': return &o_LSHIFT;
        case '.': return &o_CONCAT;
        case '*': return &o_EXP;
        default: return NULL;
        }
    }
    if (wht2 == '?') {
        switch (wht) {
        case '<': return &o_MIN;
        case '>': return &o_MAX;
        default: return NULL;
        }
    }
    return NULL;
}

MUST_CHECK Obj *compile(struct file_list_s *cflist)
{
    int wht;
    int prm = 0;
    Obj *val;

    Label *newlabel = NULL;
    size_t newmemp = 0, newmembp = 0;
    Label *tmp2 = NULL;
    Namespace *mycontext;
    address_t oaddr = 0;
    Obj *retval = NULL;

    size_t oldwaitforp = waitfor_p;
    bool nobreak = true;
    str_t labelname;
    struct anonident_s {
        uint8_t dir;
        uint8_t padding;
        uint16_t reffile;
        int32_t count;
    } anonident;
    struct {
        uint8_t type;
        uint8_t padding[3];
        line_t vline;
        struct avltree *star_tree;
    } anonident2;
    struct linepos_s epoint;
    struct file_s *cfile = cflist->file;

    while (nobreak) {
        if (mtranslate(cfile)) break; /* expand macro parameters, if any */
        newlabel = NULL;
        labelname.len = 0;ignore();epoint = lpoint; mycontext = current_context;
        if (current_section->unionmode) {
            if (current_section->address > current_section->unionend) {
                current_section->unionend = current_section->address;
                current_section->l_unionend = current_section->l_address;
            }
            current_section->l_address = current_section->l_unionstart;
            if (current_section->l_address.bank > all_mem) {
                current_section->l_address.bank &= all_mem;
                err_msg2(ERROR_ADDRESS_LARGE, NULL, &epoint);
            }
            if (current_section->address != current_section->unionstart) {
                current_section->address = current_section->unionstart;
                memjmp(&current_section->mem, current_section->address);
            }
        }
        star = (current_section->l_address.address & 0xffff) | current_section->l_address.bank;
        wht = here();
        switch (wht) {
        case '*':
            lpoint.pos++;
            switch (here()) {
            case ' ':
            case '\t':
            case ';':
            case '=':
            case '\0':
                labelname.data = (const uint8_t *)"*";labelname.len = 1;
                goto hh;
            default:
                lpoint.pos--;
            }
            break;
        case '-':
        case '+':
            lpoint.pos++;
            switch (here()) {
            case ' ':
            case '\t':
            case ';':
            case '\0':
                if (sizeof(anonident) != sizeof(anonident.dir) + sizeof(anonident.padding) + sizeof(anonident.reffile) + sizeof(anonident.count)) memset(&anonident, 0, sizeof anonident);
                else anonident.padding = 0;
                anonident.dir = (uint8_t)wht;
                anonident.reffile = reffile;
                anonident.count = (int32_t)((wht == '-') ? backr++ : forwr++);

                labelname.data = (const uint8_t *)&anonident;labelname.len = sizeof anonident;
                goto hh;
            default:
                lpoint.pos--;
            }
            break;
        default:
            break;
        }
        labelname.data = pline + lpoint.pos; labelname.len = get_label();
        if (labelname.len != 0) {
            struct linepos_s cmdpoint;
            bool islabel;
            islabel = false;
            while (here() == '.' && pline[lpoint.pos+1] != '.') {
                if ((waitfor->skip & 1) != 0) {
                    if (mycontext == current_context) {
                        if (labelname.data[0] != '_') {
                            tmp2 = find_label(&labelname, NULL);
                            if (tmp2 == NULL) {err_msg_not_defined2(&labelname, mycontext, true, &epoint); goto breakerr;}
                            tmp2->shadowcheck = true;
                        } else {
                            tmp2 = find_label2(&labelname, cheap_context);
                            if (tmp2 == NULL) {err_msg_not_defined2(&labelname, cheap_context, false, &epoint); goto breakerr;}
                        }
                    } else {
                        tmp2 = find_label2(&labelname, mycontext);
                        if (tmp2 == NULL) {err_msg_not_defined2(&labelname, mycontext, false, &epoint); goto breakerr;}
                    }
                    val = tmp2->value;
                    if (val->obj != CODE_OBJ) {
                        if (val->obj != NONE_OBJ) err_msg_wrong_type(val, CODE_OBJ, &epoint); 
                        goto breakerr;
                    }
                    if (diagnostics.case_symbol && str_cmp(&labelname, &tmp2->name) != 0) err_msg_symbol_case(&labelname, tmp2, &epoint);
                    mycontext = ((Code *)val)->names;
                }
                lpoint.pos++; islabel = true; epoint = lpoint;
                labelname.data = pline + lpoint.pos; labelname.len = get_label();
                if (labelname.len == 0) {
                    if ((waitfor->skip & 1) != 0) err_msg(ERROR_GENERL_SYNTAX, NULL);
                    goto breakerr;
                }
            }
            if (!islabel && labelname.data[0] == '_') {
                mycontext = cheap_context;
            }
            if (here() == ':' && pline[lpoint.pos + 1] != '=') {islabel = true; lpoint.pos++;}
            if (!islabel && labelname.len == 3 && (prm = lookup_opcode(labelname.data)) >=0) {
                if ((waitfor->skip & 1) != 0) goto as_opcode; else continue;
            }
            if (false) {
            hh: islabel = true;
            }
            ignore();wht = here();
            if ((waitfor->skip & 1) == 0) {epoint = lpoint; goto jn;} /* skip things if needed */
            if (labelname.len > 1 && labelname.data[0] == '_' && labelname.data[1] == '_') {err_msg2(ERROR_RESERVED_LABL, &labelname, &epoint); goto breakerr;}
            while (wht != 0 && !arguments.tasmcomp) {
                Label *label;
                bool oldreferenceit;
                struct oper_s tmp;
                Obj *result2, *val2;
                struct linepos_s epoint2, epoint3;
                int wht2 = pline[lpoint.pos + 1];

                if (wht2 == '=') {
                    if (wht == ':') {
                        if (labelname.data[0] == '*') {
                            lpoint.pos++;
                            goto starassign;
                        }
                        lpoint.pos += 2;
                        ignore();
                        goto itsvar;
                    }
                    tmp.op = oper_from_token(wht);
                    if (tmp.op == NULL) break;
                    epoint3 = lpoint;
                    lpoint.pos += 2;
                } else if (wht2 != 0 && pline[lpoint.pos + 2] == '=') {
                    tmp.op = oper_from_token2(wht, wht2);
                    if (tmp.op == NULL) break;
                    epoint3 = lpoint;
                    lpoint.pos += 3;
                } else break;

                if (labelname.data[0] == '-') {backr--;((struct anonident_s *)labelname.data)->count--;}
                else if (labelname.data[0] == '+') {forwr--;((struct anonident_s *)labelname.data)->count--;}
                ignore();
                epoint2 = lpoint;
                oldreferenceit = referenceit;
                if (labelname.data[0] == '*') {
                    label = NULL;
                    if (diagnostics.optimize) cpu_opt_invalidate();
                    val = get_star_value(current_section->l_address_val);
                } else {
                    label = find_label2(&labelname, mycontext);
                    if (label == NULL) {
                        if (tmp.op == &o_MUL) {
                            if (diagnostics.star_assign) {
                                err_msg_star_assign(&epoint3);
                                if (pline[lpoint.pos] == '*') err_msg_compound_note(&epoint3);
                            }
                            lpoint.pos = epoint3.pos;
                            wht = '*';
                            break;
                        }
                        err_msg_not_defined2(&labelname, mycontext, false, &epoint); 
                        goto breakerr;
                    }
                    if (label->constant) {
                        if (tmp.op == &o_MUL) {
                            if (diagnostics.star_assign) {
                                err_msg_star_assign(&epoint3);
                                if (pline[lpoint.pos] == '*') err_msg_compound_note(&epoint3);
                            }
                            lpoint.pos = epoint3.pos;
                            wht = '*';
                            break;
                        }
                        err_msg_not_variable(label, &labelname, &epoint); goto breakerr;
                        goto breakerr;
                    }
                    if (diagnostics.case_symbol && str_cmp(&labelname, &label->name) != 0) err_msg_symbol_case(&labelname, label, &epoint);
                    val = label->value;
                }
                if (here() == 0 || here() == ';') val2 = (Obj *)ref_addrlist(null_addrlist);
                else {
                    struct linepos_s epoints[3];
                    referenceit &= 1; /* not good... */
                    if (!get_exp(0, cfile, 0, 0, NULL)) goto breakerr;
                    val2 = get_vals_addrlist(epoints);
                    referenceit = oldreferenceit;
                }
                oaddr = current_section->address;
                tmp.v1 = val;
                tmp.v2 = val2;
                tmp.epoint = &epoint;
                tmp.epoint2 = &epoint2;
                tmp.epoint3 = &epoint3;
                result2 = tmp.v1->obj->calc2(&tmp);
                if (result2->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)result2); result2 = (Obj *)ref_none(); }
                else if (tmp.op == &o_MIN || tmp.op == &o_MAX) {
                    if (result2 == &true_value->v) val_replace(&result2, val);
                    else if (result2 == &false_value->v) val_replace(&result2, val2);
                }
                val_destroy(val2);
                if (label != NULL) {
                    listing_equal(listing, result2);
                    label->file_list = cflist;
                    label->epoint = epoint;
                    val_destroy(label->value);
                    label->value = result2;
                } else {
                    val_destroy(val);
                    starhandle(result2, &epoint, &epoint2);
                }
                goto finish;
            }
            switch (wht) {
            case '=': 
                { /* variable */
                    struct linepos_s epoints[3];
                    Label *label;
                    bool labelexists;
                    bool oldreferenceit;
                starassign:
                    oldreferenceit = referenceit;
                    if (labelname.data[0] == '*') {
                        label = NULL;
                        if (diagnostics.optimize) cpu_opt_invalidate();
                    } else label = find_label3(&labelname, mycontext, strength);
                    lpoint.pos++; ignore();
                    epoints[0] = lpoint; /* for no elements! */
                    if (here() == 0 || here() == ';') {
                        val = (Obj *)ref_addrlist(null_addrlist);
                    } else {
                        if (label != NULL && !label->ref) {
                            referenceit = false;
                        }
                        if (!get_exp(0, cfile, 0, 0, NULL)) goto breakerr;
                        val = get_vals_addrlist(epoints);
                        referenceit = oldreferenceit;
                    }
                    if (labelname.data[0] == '*') {
                        starhandle(val, &epoint, &epoints[0]);
                        goto finish;
                    }
                    if (label != NULL) {
                        labelexists = true;
                        label->unused = !label->ref;
                    } else label = new_label(&labelname, mycontext, strength, &labelexists);
                    oaddr = current_section->address;
                    listing_equal(listing, val);
                    label->ref = false;
                    if (labelexists) {
                        if (label->defpass == pass) {
                            val_destroy(val);
                            err_msg_double_defined(label, &labelname, &epoint);
                        } else {
                            if (!constcreated && temporary_label_branch == 0 && label->defpass != pass - 1) {
                                if (pass > max_pass) err_msg_cant_calculate(&label->name, &epoint);
                                constcreated = true;
                            }
                            label->constant = true;
                            label->owner = false;
                            label->file_list = cflist;
                            label->epoint = epoint;
                            const_assign(label, val);
                        }
                    } else {
                        if (!constcreated && temporary_label_branch == 0) {
                            if (pass > max_pass) err_msg_cant_calculate(&label->name, &epoint);
                            constcreated = true;
                        }
                        label->constant = true;
                        label->owner = false;
                        label->value = val;
                        label->file_list = cflist;
                        label->epoint = epoint;
                    }
                    goto finish;
                }
            case '.':
                cmdpoint = lpoint;
                prm = get_command();
                ignore();
                if (labelname.data[0] == '*') {
                    err_msg2(ERROR_RESERVED_LABL, &labelname, &epoint);
                    newlabel = NULL; epoint = cmdpoint; goto as_command;
                }
                switch (prm) {
                case CMD_VAR: /* variable */
                    {
                        Label *label;
                        bool labelexists;
                        bool oldreferenceit;
                    itsvar:
                        oldreferenceit = referenceit;
                        label = find_label3(&labelname, mycontext, strength);
                        if (here() == 0 || here() == ';') val = (Obj *)ref_addrlist(null_addrlist);
                        else {
                            struct linepos_s epoints[3];
                            referenceit &= 1; /* not good... */
                            if (!get_exp(0, cfile, 0, 0, NULL)) goto breakerr;
                            val = get_vals_addrlist(epoints);
                            referenceit = oldreferenceit;
                        }
                        if (label != NULL) {
                            labelexists = true;
                            if (diagnostics.case_symbol && str_cmp(&labelname, &label->name) != 0) err_msg_symbol_case(&labelname, label, &epoint);
                        } else label = new_label(&labelname, mycontext, strength, &labelexists);
                        oaddr = current_section->address;
                        listing_equal(listing, val);
                        if (labelexists) {
                            if (label->constant) {
                                err_msg_double_defined(label, &labelname, &epoint);
                                val_destroy(val);
                            } else {
                                label->owner = false;
                                label->file_list = cflist;
                                label->epoint = epoint;
                                if (label->defpass != pass) {
                                    label->ref = false;
                                    label->defpass = pass;
                                }
                                val_destroy(label->value);
                                label->value = val;
                            }
                        } else {
                            label->constant = false;
                            label->owner = false;
                            label->value = val;
                            label->file_list = cflist;
                            label->epoint = epoint;
                        }
                        goto finish;
                    }
                case CMD_LBL:
                    { /* label */
                        Label *label;
                        Lbl *lbl;
                        bool labelexists;
                        listing_line(listing, 0);
                        label = new_label(&labelname, mycontext, strength, &labelexists);
                        lbl = (Lbl *)val_alloc(LBL_OBJ);
                        lbl->sline = lpoint.line;
                        lbl->waitforp = waitfor_p;
                        lbl->file_list = cflist;
                        lbl->parent = current_context;
                        if (labelexists) {
                            if (label->defpass == pass) {
                                val_destroy(&lbl->v);
                                err_msg_double_defined(label, &labelname, &epoint);
                            } else {
                                if (!constcreated && temporary_label_branch == 0 && label->defpass != pass - 1) {
                                    if (pass > max_pass) err_msg_cant_calculate(&label->name, &epoint);
                                    constcreated = true;
                                }
                                label->constant = true;
                                label->owner = true;
                                label->file_list = cflist;
                                label->epoint = epoint;
                                label->unused = !label->ref;
                                const_assign(label, &lbl->v);
                            }
                        } else {
                            if (!constcreated && temporary_label_branch == 0) {
                                if (pass > max_pass) err_msg_cant_calculate(&label->name, &epoint);
                                constcreated = true;
                            }
                            label->constant = true;
                            label->owner = true;
                            label->value = &lbl->v;
                            label->file_list = cflist;
                            label->epoint = epoint;
                        }
                        if (!arguments.tasmcomp && diagnostics.deprecated) err_msg2(ERROR______OLD_GOTO, NULL, &cmdpoint);
                        label->ref = false;
                        goto finish;
                    }
                case CMD_MACRO:/* .macro */
                case CMD_SEGMENT:
                    {
                        Label *label;
                        Macro *macro;
                        Type *obj = (prm == CMD_MACRO) ? MACRO_OBJ : SEGMENT_OBJ;
                        bool labelexists;
                        listing_line(listing, 0);
                        new_waitfor(W_ENDM, &cmdpoint);waitfor->skip = 0;
                        label = new_label(&labelname, mycontext, strength, &labelexists);
                        macro = (Macro *)val_alloc(obj);
                        macro->file_list = cflist;
                        macro->line = epoint.line;
                        get_macro_params(&macro->v);
                        if (labelexists) {
                            if (label->value->obj == obj) macro->retval = ((Macro *)label->value)->retval;
                            if (label->defpass == pass) {
                                waitfor->val = &macro->v;
                                err_msg_double_defined(label, &labelname, &epoint);
                            } else {
                                if (!constcreated && temporary_label_branch == 0 && label->defpass != pass - 1) {
                                    if (pass > max_pass) err_msg_cant_calculate(&label->name, &epoint);
                                    constcreated = true;
                                }
                                label->constant = true;
                                label->owner = true;
                                label->file_list = cflist;
                                label->epoint = epoint;
                                label->unused = !label->ref;
                                const_assign(label, &macro->v);
                                waitfor->val = val_reference(label->value);
                            }
                        } else {
                            macro->retval = false;
                            if (!constcreated && temporary_label_branch == 0) {
                                if (pass > max_pass) err_msg_cant_calculate(&label->name, &epoint);
                                constcreated = true;
                            }
                            label->constant = true;
                            label->owner = true;
                            label->value = &macro->v;
                            label->file_list = cflist;
                            label->epoint = epoint;
                            waitfor->val = val_reference(&macro->v);
                        } 
                        label->ref = false;
                        goto finish;
                    }
                case CMD_FUNCTION:
                    {
                        Label *label;
                        Mfunc *mfunc;
                        bool labelexists;
                        listing_line(listing, 0);
                        new_waitfor(W_ENDF, &cmdpoint);waitfor->skip = 0;
                        if (temporary_label_branch != 0) {err_msg2(ERROR___NOT_ALLOWED, ".function", &cmdpoint);goto breakerr;}
                        label = new_label(&labelname, mycontext, strength, &labelexists);
                        mfunc = (Mfunc *)val_alloc(MFUNC_OBJ);
                        mfunc->file_list = cflist;
                        mfunc->line = epoint.line;
                        mfunc->argc = 0;
                        mfunc->param = NULL; /* might be recursive through init */
                        mfunc->nslen = 0;
                        mfunc->namespaces = NULL;
                        if (labelexists) {
                            if (label->defpass == pass) {
                                val_destroy(&mfunc->v);
                                err_msg_double_defined(label, &labelname, &epoint);
                            } else {
                                if (!constcreated && temporary_label_branch == 0 && label->defpass != pass - 1) {
                                    if (pass > max_pass) err_msg_cant_calculate(&label->name, &epoint);
                                    constcreated = true;
                                }
                                label->constant = true;
                                label->owner = true;
                                label->file_list = cflist;
                                label->epoint = epoint;
                                get_func_params(mfunc, cfile);
                                get_namespaces(mfunc);
                                label->unused = !label->ref;
                                const_assign(label, &mfunc->v);
                            }
                        } else {
                            if (!constcreated && temporary_label_branch == 0) {
                                if (pass > max_pass) err_msg_cant_calculate(&label->name, &epoint);
                                constcreated = true;
                            }
                            label->constant = true;
                            label->owner = true;
                            label->value = &mfunc->v;
                            label->file_list = cflist;
                            label->epoint = epoint;
                            get_func_params(mfunc, cfile);
                            get_namespaces(mfunc);
                        }
                        label->ref = false;
                        goto finish;
                    }
                case CMD_STRUCT:
                case CMD_UNION:
                    {
                        Label *label;
                        Struct *structure;
                        struct section_s olds = *current_section;
                        bool labelexists, doubledef = false;
                        Type *obj = (prm == CMD_STRUCT) ? STRUCT_OBJ : UNION_OBJ;

                        if (diagnostics.optimize) cpu_opt_invalidate();
                        new_waitfor((prm==CMD_STRUCT)?W_ENDS:W_ENDU, &cmdpoint);waitfor->skip = 0;
                        label = new_label(&labelname, mycontext, strength, &labelexists);

                        current_section->provides = ~(uval_t)0;current_section->requires = current_section->conflicts = 0;
                        current_section->end = current_section->start = current_section->restart = current_section->address = 0;
                        current_section->l_restart.address = current_section->l_restart.bank = 0;
                        current_section->l_address.address = current_section->l_address.bank = 0;
                        current_section->dooutput = false;memjmp(&current_section->mem, 0);
                        current_section->l_address_val = (Obj *)ref_int(int_value[0]);

                        structure = (Struct *)val_alloc(obj);
                        structure->file_list = cflist;
                        structure->line = epoint.line;
                        get_macro_params(&structure->v);
                        if (labelexists) {
                            if (label->value->obj == obj) structure->retval = ((Struct *)label->value)->retval;
                            if (label->defpass == pass) {
                                doubledef = true;
                                structure->size = 0;
                                structure->names = new_namespace(cflist, &epoint);
                                err_msg_double_defined(label, &labelname, &epoint);
                            } else {
                                if (!constcreated && temporary_label_branch == 0 && label->defpass != pass - 1) {
                                    if (pass > max_pass) err_msg_cant_calculate(&label->name, &epoint);
                                    constcreated = true;
                                }
                                label->constant = true;
                                label->owner = true;
                                label->file_list = cflist;
                                label->epoint = epoint;
                                if (label->value->obj == obj) {
                                    Struct *prev = (Struct *)label->value;
                                    structure->size = prev->size;
                                    structure->names = ref_namespace(prev->names);
                                } else {
                                    structure->size = 0;
                                    structure->names = new_namespace(cflist, &epoint);
                                }
                                label->unused = !label->ref;
                                const_assign(label, &structure->v);
                                structure = (Struct *)label->value;
                            }
                        } else {
                            structure->retval = false;
                            if (!constcreated && temporary_label_branch == 0) {
                                if (pass > max_pass) err_msg_cant_calculate(&label->name, &epoint);
                                constcreated = true;
                            }
                            label->constant = true;
                            label->owner = true;
                            label->value = &structure->v;
                            label->file_list = cflist;
                            label->epoint = epoint;
                            structure->size = 0;
                            structure->names = new_namespace(cflist, &epoint);
                        }
                        label->ref = false;
                        listing_line(listing, cmdpoint.pos);
                        current_section->structrecursion++;
                        if (current_section->structrecursion<100) {
                            bool old_unionmode = current_section->unionmode;
                            address_t old_unionstart = current_section->unionstart, old_unionend = current_section->unionend;
                            address2_t old_l_unionstart = current_section->l_unionstart, old_l_unionend = current_section->l_unionend;
                            current_section->unionmode = (prm==CMD_UNION);
                            current_section->unionstart = current_section->unionend = current_section->address;
                            current_section->l_unionstart = current_section->l_unionend = current_section->l_address;
                            waitfor->what = (prm == CMD_STRUCT) ? W_ENDS2 : W_ENDU2;
                            waitfor->skip = 1;
                            val = macro_recurse(W_ENDS, &structure->v, structure->names, &lpoint);
                            if (val != NULL) val_destroy(val);
                            current_section->unionmode = old_unionmode;
                            current_section->unionstart = old_unionstart; current_section->unionend = old_unionend;
                            current_section->l_unionstart = old_l_unionstart; current_section->l_unionend = old_l_unionend;
                        } else err_msg2(ERROR__MACRECURSION, NULL, &cmdpoint);
                        current_section->structrecursion--;

                        if (doubledef) val_destroy(&structure->v);
                        else if (structure->size != (current_section->address & all_mem2)) {
                            structure->size = current_section->address & all_mem2;
                            if (label->usepass >= pass) {
                                if (fixeddig && pass > max_pass) err_msg_cant_calculate(&label->name, &label->epoint);
                                fixeddig = false;
                            }
                        }
                        current_section->provides = olds.provides;current_section->requires = olds.requires;current_section->conflicts = olds.conflicts;
                        current_section->end = olds.end;current_section->start = olds.start;current_section->restart = olds.restart;
                        current_section->l_restart = olds.l_restart;current_section->address = olds.address;current_section->l_address = olds.l_address;
                        current_section->dooutput = olds.dooutput;memjmp(&current_section->mem, current_section->address);
                        val_destroy(current_section->l_address_val);
                        current_section->l_address_val = olds.l_address_val;
                        if (current_section->l_address.bank > all_mem) {
                            current_section->l_address.bank &= all_mem;
                            err_msg2(ERROR_ADDRESS_LARGE, NULL, &epoint);
                        }
                        goto breakerr;
                    }
                case CMD_SECTION:
                    {
                        struct section_s *tmp;
                        str_t sectionname;
                        struct linepos_s opoint;
                        new_waitfor(W_SEND, &cmdpoint);waitfor->section = current_section;
                        opoint = lpoint;
                        sectionname.data = pline + lpoint.pos; sectionname.len = get_label();
                        if (sectionname.len == 0) {err_msg2(ERROR_LABEL_REQUIRE, NULL, &opoint); goto breakerr;}
                        if (current_section->structrecursion != 0 || !current_section->dooutput) {err_msg2(ERROR___NOT_ALLOWED, ".section", &epoint); goto breakerr;}
                        tmp = find_new_section(&sectionname);
                        if (tmp->usepass == 0 || tmp->defpass < pass - 1) {
                            tmp->end = tmp->start = tmp->restart = tmp->address = 0;
                            tmp->size = tmp->l_restart.address = tmp->l_restart.bank = tmp->l_address.address = tmp->l_address.bank = 0;
                            if (tmp->usepass != 0 && tmp->usepass >= pass - 1) err_msg_not_defined(&sectionname, &opoint);
                            else if (fixeddig && pass > max_pass) err_msg_cant_calculate(&sectionname, &opoint);
                            fixeddig = false;
                            tmp->defpass = pass - 1;
                            restart_memblocks(&tmp->mem, tmp->address);
                            if (diagnostics.optimize) cpu_opt_invalidate();
                        } else if (tmp->usepass != pass) {
                            if (!tmp->moved) {
                                if (tmp->end < tmp->address) tmp->end = tmp->address;
                                tmp->moved = true;
                            }
                            tmp->size = tmp->end - tmp->start;
                            tmp->end = tmp->start = tmp->restart;
                            tmp->wrapwarn = false;
                            tmp->address = tmp->restart;
                            tmp->l_address = tmp->l_restart;
                            restart_memblocks(&tmp->mem, tmp->address);
                            if (diagnostics.optimize) cpu_opt_invalidate();
                        }
                        tmp->usepass = pass;
                        waitfor->what = W_SEND2;
                        current_section = tmp; star = (current_section->l_address.address & 0xffff) | current_section->l_address.bank;
                        break;
                    }
                }
                break;
            }
            {
                bool labelexists = false;
                Code *code;
                if (labelname.data[0] == '*') {
                    err_msg2(ERROR_RESERVED_LABL, &labelname, &epoint);
                    newlabel = NULL;
                    epoint = lpoint;
                    goto jn;
                }
                if (!islabel) {
                    Namespace *parent;
                    bool down = (labelname.data[0] != '_');
                    if (down) tmp2 = find_label(&labelname, &parent);
                    else {
                        parent = cheap_context;
                        tmp2 = find_label2(&labelname, cheap_context);
                    }
                    if (tmp2 != NULL) {
                        Type *obj = tmp2->value->obj;
                        if (diagnostics.case_symbol && str_cmp(&labelname, &tmp2->name) != 0) err_msg_symbol_case(&labelname, tmp2, &epoint);
                        if (obj == MACRO_OBJ || obj == SEGMENT_OBJ || obj == MFUNC_OBJ) {
                            if (down) tmp2->shadowcheck = true;
                            labelname.len = 0;val = tmp2->value; goto as_macro;
                        }
                        if (parent == mycontext && tmp2->strength == strength) {
                            newlabel = tmp2;
                            labelexists = true;
                        }
                    }
                }
                if (!labelexists) newlabel = new_label(&labelname, mycontext, strength, &labelexists);
                oaddr = current_section->address;
                if (labelexists) {
                    if (newlabel->defpass == pass) {
                        err_msg_double_defined(newlabel, &labelname, &epoint);
                        newlabel = NULL;
                        if (wht == '.') {
                            epoint = cmdpoint;
                            goto as_command;
                        }
                        epoint = lpoint;
                        goto jn;
                    }
                    if (!newlabel->update_after && newlabel->value->obj != CODE_OBJ) {
                        val_destroy(newlabel->value);
                        labelexists = false;
                        newlabel->defpass = pass;
                    }
                }
                if (labelexists) {
                    if (!constcreated && temporary_label_branch == 0 && newlabel->defpass != pass - 1) {
                        if (pass > max_pass) err_msg_cant_calculate(&newlabel->name, &epoint);
                        constcreated = true;
                    }
                    newlabel->constant = true;
                    newlabel->owner = true;
                    newlabel->file_list = cflist;
                    newlabel->epoint = epoint;
                    if (!newlabel->update_after) {
                        Obj *tmp;
                        if (diagnostics.optimize && newlabel->ref) cpu_opt_invalidate();
                        newlabel->unused = !newlabel->ref;
                        tmp = get_star_value(current_section->l_address_val);
                        code = (Code *)newlabel->value;
                        if (!tmp->obj->same(tmp, code->addr)) {
                            val_destroy(code->addr); code->addr = tmp;
                            if (newlabel->usepass >= pass) {
                                if (fixeddig && pass > max_pass) err_msg_cant_calculate(&newlabel->name, &epoint);
                                fixeddig = false;
                            }
                        } else val_destroy(tmp);
                        if (code->requires != current_section->requires || code->conflicts != current_section->conflicts || code->offs != 0) {
                            code->requires = current_section->requires;
                            code->conflicts = current_section->conflicts;
                            code->offs = 0;
                            if (newlabel->usepass >= pass) {
                                if (fixeddig && pass > max_pass) err_msg_cant_calculate(&newlabel->name, &epoint);
                                fixeddig = false;
                            }
                        }
                        get_mem(&current_section->mem, &newmemp, &newmembp);
                        code->apass = pass;
                        newlabel->defpass = pass;
                    }
                } else {
                    if (diagnostics.optimize) cpu_opt_invalidate();
                    code = new_code();
                    if (!constcreated && temporary_label_branch == 0) {
                        if (pass > max_pass) err_msg_cant_calculate(&newlabel->name, &epoint);
                        constcreated = true;
                    }
                    newlabel->constant = true;
                    newlabel->owner = true;
                    newlabel->value = (Obj *)code;
                    newlabel->file_list = cflist;
                    newlabel->epoint = epoint;
                    code->addr = get_star_value(current_section->l_address_val);
                    code->size = 0;
                    code->offs = 0;
                    code->dtype = D_NONE;
                    code->pass = 0;
                    code->apass = pass;
                    code->names = new_namespace(cflist, &epoint);
                    code->requires = current_section->requires;
                    code->conflicts = current_section->conflicts;
                    get_mem(&current_section->mem, &newmemp, &newmembp);
                }
            }
            if (wht == '.') { /* .proc */
                epoint = cmdpoint;
                switch (prm) {
                case CMD_PROC:
                    listing_line(listing, epoint.pos);
                    new_waitfor(W_PEND, &epoint);waitfor->label = newlabel;waitfor->addr = current_section->address;waitfor->memp = newmemp;waitfor->membp = newmembp;
                    if (!newlabel->ref && ((Code *)newlabel->value)->pass != 0) {waitfor->skip = 0; set_size(newlabel, 0, &current_section->mem, newmemp, newmembp);}
                    else {         /* TODO: first time it should not compile */
                        push_context(((Code *)newlabel->value)->names);
                        newlabel->ref = false;
                    }
                    newlabel = NULL;
                    goto finish;
                case CMD_DSTRUCT: /* .dstruct */
                case CMD_DUNION:
                    {
                        bool old_unionmode = current_section->unionmode;
                        struct values_s *vs;
                        Type *obj;
                        Namespace *context;
                        address_t old_unionstart = current_section->unionstart, old_unionend = current_section->unionend;
                        address2_t old_l_unionstart = current_section->l_unionstart, old_l_unionend = current_section->l_unionend;

                        if (diagnostics.optimize) cpu_opt_invalidate();
                        listing_line(listing, epoint.pos);
                        newlabel->ref = false;
                        if (!get_exp(1, cfile, 1, 0, &epoint)) goto breakerr;
                        vs = get_val(); val = vs->val;
                        if (val->obj == ERROR_OBJ) { err_msg_output((Error *)val); goto finish; }
                        if (val == &none_value->v) { err_msg_still_none(NULL, &vs->epoint); goto finish;}
                        obj = (prm == CMD_DSTRUCT) ? STRUCT_OBJ : UNION_OBJ;
                        if (val->obj != obj) {err_msg_wrong_type(val, obj, &vs->epoint); goto breakerr;}
                        ignore();if (here() == ',') lpoint.pos++;
                        current_section->structrecursion++;
                        current_section->unionmode = (prm==CMD_DUNION);
                        current_section->unionstart = current_section->unionend = current_section->address;
                        current_section->l_unionstart = current_section->l_unionend = current_section->l_address;
                        if (!((Struct *)val)->retval && newlabel->value->obj == CODE_OBJ) {
                            context = ((Code *)newlabel->value)->names;
                        } else {
                            Label *label;
                            bool labelexists;
                            str_t tmpname;
                            if (sizeof(anonident2) != sizeof(anonident2.type) + sizeof(anonident2.padding) + sizeof(anonident2.star_tree) + sizeof(anonident2.vline)) memset(&anonident2, 0, sizeof anonident2);
                            else anonident2.padding[0] = anonident2.padding[1] = anonident2.padding[2] = 0;
                            anonident2.type = '#';
                            anonident2.star_tree = star_tree;
                            anonident2.vline = vline;
                            tmpname.data = (const uint8_t *)&anonident2; tmpname.len = sizeof anonident2;
                            label = new_label(&tmpname, mycontext, strength, &labelexists);
                            if (labelexists) {
                                if (label->defpass == pass) err_msg_double_defined(label, &tmpname, &epoint);
                                label->constant = true;
                                label->owner = true;
                                label->defpass = pass;
                                if (label->value->obj != NAMESPACE_OBJ) {
                                    val_destroy(label->value);
                                    label->value = (Obj *)new_namespace(cflist, &epoint);
                                }
                            } else {
                                label->constant = true;
                                label->owner = true;
                                label->value = (Obj *)new_namespace(cflist, &epoint);
                                label->file_list = cflist;
                                label->epoint = epoint;
                            } 
                            context = (Namespace *)label->value;
                        }
                        val = macro_recurse((prm==CMD_DSTRUCT)?W_ENDS2:W_ENDU2, val, context, &epoint);
                        if (val != NULL) {
                            if (newlabel != NULL) {
                                newlabel->update_after = true;
                                const_assign(newlabel, val);
                            } else val_destroy(val);
                        }
                        current_section->structrecursion--;
                        current_section->unionmode = old_unionmode;
                        current_section->unionstart = old_unionstart; current_section->unionend = old_unionend;
                        current_section->l_unionstart = old_l_unionstart; current_section->l_unionend = old_l_unionend;
                        goto breakerr;
                    }
                case CMD_SECTION:
                    waitfor->label = newlabel;waitfor->addr = current_section->address;waitfor->memp = newmemp;waitfor->membp = newmembp;
                    listing_line(listing, epoint.pos);
                    newlabel->ref = false;
                    newlabel = NULL;
                    goto finish;
                }
                newlabel->ref = false;
                goto as_command;
            }
            if (labelname.len == 3 && !newlabel->ref && epoint.pos != 0 && diagnostics.label_left) {
                unsigned int i;
                for (i = 0; i < 3; i++) {
                    uint8_t c = labelname.data[i] | arguments.caseinsensitive;
                    if ((c < 'a') || (c > 'z')) break;
                }
                if (i == 3) err_msg_label_left(&epoint);
            }
            epoint = lpoint;
            newlabel->ref = false;
        }
        jn:
        switch (wht) {
        case '>':
        case '<':
        case '*':
            if ((waitfor->skip & 1) != 0) {
                if (pline[lpoint.pos + 1] != wht || pline[lpoint.pos + 2] != '=' || arguments.tasmcomp) {
                    if (wht == '*') {
                        lpoint.pos++;ignore();
                        if (here() == '=') {
                            labelname.data = (const uint8_t *)"*";labelname.len = 1;
                            goto starassign;
                        }
                    }
                    err_msg2(ERROR_GENERL_SYNTAX, NULL, &epoint);
                } else err_msg2(ERROR_LABEL_REQUIRE, NULL, &epoint);
                goto breakerr;
            }
            break;
        case '|':
        case '&':
            if ((waitfor->skip & 1) != 0) {
                if (pline[lpoint.pos + 1] == wht && pline[lpoint.pos + 2] == '=' && !arguments.tasmcomp) {
                    err_msg2(ERROR_LABEL_REQUIRE, NULL, &epoint);
                    goto breakerr;
                }
            }
            /* fall through */
        case '+':
        case '-':
        case '/':
        case '%':
        case '^':
        case ':':
            if ((waitfor->skip & 1) != 0) {
                if (pline[lpoint.pos + 1] != '=' || arguments.tasmcomp) {
                    err_msg2(ERROR_GENERL_SYNTAX, NULL, &epoint);
                    goto breakerr;
                }
            }
            /* fall through */
        case '=':
            if ((waitfor->skip & 1) != 0) {err_msg2(ERROR_LABEL_REQUIRE, NULL, &epoint); goto breakerr;}
            break;
        case ';':
        case '\0':
            if ((waitfor->skip & 1) != 0) {
                if (newlabel != NULL && newlabel->value->obj == CODE_OBJ && labelname.len != 0 && labelname.data[0] != '_' && labelname.data[0] != '+' && labelname.data[0] != '-') {val_destroy(&cheap_context->v);cheap_context = ref_namespace(((Code *)newlabel->value)->names);}
                listing_line(listing, epoint.pos);
            }
            break;
        case '.':
            prm = get_command();
            ignore();
        as_command:
            switch (prm) {
            case CMD_ENDC: /* .endc */
                if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                if (!close_waitfor(W_ENDC)) err_msg2(ERROR______EXPECTED,".comment", &epoint);
                if ((waitfor->skip & 1) != 0) listing_line_cut2(listing, epoint.pos);
                break;
            case CMD_ENDIF: /* .endif */
            case CMD_FI: /* .fi */
                {
                    if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                    if (!close_waitfor(W_FI2) && !close_waitfor(W_FI)) err_msg2(ERROR______EXPECTED,".if", &epoint);
                    if ((waitfor->skip & 1) != 0) listing_line_cut2(listing, epoint.pos);
                }
                break;
            case CMD_ENDSWITCH: /* .endswitch */
                {
                    if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                    if (!close_waitfor(W_SWITCH2) && !close_waitfor(W_SWITCH)) err_msg2(ERROR______EXPECTED,".switch", &epoint);
                    if ((waitfor->skip & 1) != 0) listing_line_cut2(listing, epoint.pos);
                }
                break;
            case CMD_DEFAULT: /* .default */
                {
                    if ((waitfor->skip & 1) != 0) listing_line_cut(listing, epoint.pos);
                    if (waitfor->what==W_SWITCH) {err_msg2(ERROR______EXPECTED,".endswitch", &epoint); break;}
                    if (waitfor->what!=W_SWITCH2) {err_msg2(ERROR______EXPECTED,".switch", &epoint); break;}
                    waitfor->skip = waitfor->skip >> 1;
                    waitfor->what = W_SWITCH;waitfor->epoint = epoint;
                    if ((waitfor->skip & 1) != 0) listing_line_cut2(listing, epoint.pos);
                }
                break;
            case CMD_ELSE: /* .else */
                {
                    if ((waitfor->skip & 1) != 0) listing_line_cut(listing, epoint.pos);
                    if (waitfor->what==W_FI) { err_msg2(ERROR______EXPECTED,".fi", &epoint); break; }
                    if (waitfor->what!=W_FI2) { err_msg2(ERROR______EXPECTED,".if", &epoint); break; }
                    waitfor->skip = waitfor->skip >> 1;
                    waitfor->what = W_FI;waitfor->epoint = epoint;
                    if ((waitfor->skip & 1) != 0) listing_line_cut2(listing, epoint.pos);
                }
                break;
            case CMD_IF: /* .if */
            case CMD_IFEQ: /* .ifeq */
            case CMD_IFNE: /* .ifne */
            case CMD_IFPL: /* .ifpl */
            case CMD_IFMI: /* .ifmi */
                {
                    uint8_t skwait = waitfor->skip;
                    ival_t ival;
                    bool truth;
                    Obj *err;
                    struct values_s *vs;
                    if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                    new_waitfor(W_FI2, &epoint);
                    if (skwait != 1) { waitfor->skip = 0; break; }
                    if (!get_exp(0, cfile, 1, 1, &epoint)) { waitfor->skip = 0; goto breakerr;}
                    vs = get_val(); val = vs->val;
                    switch (prm) {
                    case CMD_IF:
                        if (tobool(vs, &truth)) { waitfor->skip = 0; break; }
                        waitfor->skip = truth ? (prevwaitfor->skip & 1) : (uint8_t)((prevwaitfor->skip & 1) << 1);
                        break;
                    case CMD_IFNE:
                    case CMD_IFEQ:
                        err = val->obj->sign(val, &vs->epoint);
                        if (err->obj != INT_OBJ) {
                            if (err == &none_value->v) err_msg_still_none(NULL, &vs->epoint);
                            else if (err->obj == ERROR_OBJ) err_msg_output((Error *)err);
                            val_destroy(err);
                            waitfor->skip = 0; break;
                        }
                        waitfor->skip = ((((Int *)err)->len == 0) != (prm == CMD_IFNE)) ? (prevwaitfor->skip & 1) : (uint8_t)((prevwaitfor->skip & 1) << 1);
                        val_destroy(err);
                        break;
                    case CMD_IFPL:
                    case CMD_IFMI:
                        if (arguments.tasmcomp) {
                            if (toival(val, &ival, 8 * sizeof ival, &vs->epoint)) { waitfor->skip = 0; break; }
                            waitfor->skip = (((ival & 0x8000) == 0) != (prm == CMD_IFMI)) ? (prevwaitfor->skip & 1) : (uint8_t)((prevwaitfor->skip & 1) << 1);
                        } else {
                            err = val->obj->sign(val, &vs->epoint);
                            if (err->obj != INT_OBJ) {
                                if (err == &none_value->v) err_msg_still_none(NULL, &vs->epoint);
                                else if (err->obj == ERROR_OBJ) err_msg_output((Error *)err);
                                val_destroy(err);
                                waitfor->skip = 0; break;
                            }
                            waitfor->skip = ((((Int *)err)->len >= 0) != (prm == CMD_IFMI)) ? (prevwaitfor->skip & 1) : (uint8_t)((prevwaitfor->skip & 1) << 1);
                            val_destroy(err);
                        }
                        break;
                    }
                }
                break;
            case CMD_ELSIF: /* .elsif */
                {
                    uint8_t skwait = waitfor->skip;
                    bool truth;
                    struct values_s *vs;
                    if ((waitfor->skip & 1) != 0) listing_line_cut(listing, epoint.pos);
                    if (waitfor->what != W_FI2) {err_msg2(ERROR______EXPECTED, ".if", &epoint); break;}
                    waitfor->epoint = epoint;
                    if (skwait == 2) {
                        if (!get_exp(0, cfile, 1, 1, &epoint)) { waitfor->skip = 0; goto breakerr;}
                        vs = get_val();
                    } else { waitfor->skip = 0; break; }
                    if (tobool(vs, &truth)) { waitfor->skip = 0; break; }
                    waitfor->skip = truth ? (waitfor->skip >> 1) : (waitfor->skip & 2);
                    if ((waitfor->skip & 1) != 0) listing_line_cut2(listing, epoint.pos);
                }
                break;
            case CMD_SWITCH: /* .switch */
                {
                    uint8_t skwait = waitfor->skip;
                    if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                    new_waitfor(W_SWITCH2, &epoint);
                    if (skwait == 1) {
                        struct values_s *vs;
                        if (!get_exp(0, cfile, 1, 1, &epoint)) {waitfor->skip = 0; goto breakerr;}
                        vs = get_val(); val = vs->val;
                        if (val->obj == ERROR_OBJ) { err_msg_output((Error *)val); val = (Obj *)none_value; }
                        else if (val == &none_value->v) err_msg_still_none(NULL, &vs->epoint);
                    } else val = (Obj *)none_value;
                    waitfor->val = val_reference(val);
                    waitfor->skip = (val == &none_value->v) ? 0 : (uint8_t)((prevwaitfor->skip & 1) << 1);
                }
                break;
            case CMD_CASE: /* .case */
                {
                    uint8_t skwait = waitfor->skip;
                    bool truth = false;
                    if ((waitfor->skip & 1) != 0) listing_line_cut(listing, epoint.pos);
                    if (waitfor->what == W_SWITCH) { err_msg2(ERROR______EXPECTED,".endswitch", &epoint); goto breakerr; }
                    if (waitfor->what != W_SWITCH2) { err_msg2(ERROR______EXPECTED,".switch", &epoint); goto breakerr; }
                    waitfor->epoint = epoint;
                    if (skwait==2 || diagnostics.switch_case) {
                        struct values_s *vs;
                        Obj *result2;
                        struct oper_s tmp;
                        if (!get_exp(0, cfile, 1, 0, &epoint)) { waitfor->skip = 0; goto breakerr; }
                        tmp.op = &o_EQ;
                        tmp.epoint = tmp.epoint3 = &epoint;
                        while (!truth && (vs = get_val()) != NULL) {
                            val = vs->val;
                            if (val->obj == ERROR_OBJ) { err_msg_output((Error *)val); continue; }
                            if (val == &none_value->v) { err_msg_still_none(NULL, &vs->epoint);continue; }
                            tmp.v1 = waitfor->val;
                            tmp.v2 = val;
                            tmp.epoint2 = &vs->epoint;
                            result2 = tmp.v1->obj->calc2(&tmp);
                            truth = (Bool *)result2 == true_value;
                            val_destroy(result2);
                            if (truth && diagnostics.switch_case && skwait != 2) {
                                err_msg2(ERROR_DUPLICATECASE, NULL, &vs->epoint);
                                truth = false;
                                break;
                            }
                        }
                    }
                    waitfor->skip = truth ? (waitfor->skip >> 1) : (waitfor->skip & 2);
                    if ((waitfor->skip & 1) != 0) listing_line_cut2(listing, epoint.pos);
                }
                break;
            case CMD_ENDM: /* .endm */
                if (waitfor->what==W_ENDM) {
                    if (waitfor->val != NULL) ((Macro *)waitfor->val)->retval = (here() != 0 && here() != ';');
                    close_waitfor(W_ENDM);
                    if ((waitfor->skip & 1) != 0) listing_line_cut2(listing, epoint.pos);
                    goto breakerr;
                } else if (close_waitfor(W_ENDM2)) {
                    nobreak = false;
                    if (here() != 0 && here() != ';' && get_exp(0, cfile, 0, 0, NULL)) {
                        retval = get_vals_tuple();
                    }
                } else {
                    err_msg2(ERROR______EXPECTED,".macro or .segment", &epoint);
                    goto breakerr;
                }
                break;
            case CMD_ENDF: /* .endf */
                if (close_waitfor(W_ENDF)) {
                    if ((waitfor->skip & 1) != 0) listing_line_cut2(listing, epoint.pos);
                    goto breakerr;
                } else if (close_waitfor(W_ENDF2)) {
                    nobreak = false;
                    if (here() != 0 && here() != ';' && get_exp(0, cfile, 0, 0, NULL)) {
                        retval = get_vals_tuple();
                    }
                } else {
                    err_msg2(ERROR______EXPECTED,".function", &epoint);
                    goto breakerr;
                }
                break;
            case CMD_NEXT: /* .next */
                waitfor->epoint = epoint;
                if (close_waitfor(W_NEXT)) {
                    if ((waitfor->skip & 1) != 0) listing_line_cut2(listing, epoint.pos);
                } else if (waitfor->what == W_NEXT2) {
                    retval = (Obj *)true_value; /* anything non-null */
                    nobreak = false;
                } else err_msg2(ERROR______EXPECTED,".for or .rept", &epoint);
                break;
            case CMD_PEND: /* .pend */
                if (waitfor->what==W_PEND) {
                    if ((waitfor->skip & 1) != 0) {
                        listing_line(listing, epoint.pos);
                        if (pop_context()) err_msg2(ERROR______EXPECTED,".proc", &epoint);
                        if (waitfor->label != NULL) set_size(waitfor->label, current_section->address - waitfor->addr, &current_section->mem, waitfor->memp, waitfor->membp);
                    }
                    close_waitfor(W_PEND);
                    if ((waitfor->skip & 1) != 0) listing_line_cut2(listing, epoint.pos);
                } else err_msg2(ERROR______EXPECTED,".proc", &epoint);
                break;
            case CMD_ENDS: /* .ends */
                if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                if (waitfor->what==W_ENDS) {
                    if ((waitfor->skip & 1) != 0) {
                        current_section->unionmode = waitfor->breakout;
                        close_waitfor(W_ENDS);
                        break;
                    }
                    close_waitfor(W_ENDS);
                    goto breakerr;
                }
                if (close_waitfor(W_ENDS2)) {
                    nobreak = false;
                    if (here() != 0 && here() != ';' && get_exp(0, cfile, 0, 0, NULL)) {
                        retval = get_vals_tuple();
                    }
                    break;
                }
                err_msg2(ERROR______EXPECTED,".struct", &epoint);
                goto breakerr;
            case CMD_SEND: /* .send */
                if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                if (close_waitfor(W_SEND)) {
                    get_label();
                } else if (waitfor->what==W_SEND2) {
                    str_t sectionname;
                    epoint = lpoint;
                    sectionname.data = pline + lpoint.pos; sectionname.len = get_label();
                    if (sectionname.len != 0) {
                        str_t cf;
                        str_cfcpy(&cf, &sectionname);
                        if (str_cmp(&cf, &current_section->cfname) != 0) {
                            char *s = (char *)mallocx(current_section->name.len + 1);
                            memcpy(s, current_section->name.data, current_section->name.len);
                            s[current_section->name.len] = '\0';
                            err_msg2(ERROR______EXPECTED, s, &epoint);
                            free(s);
                        }
                    }
                    if (waitfor->label != NULL) set_size(waitfor->label, current_section->address - waitfor->addr, &current_section->mem, waitfor->memp, waitfor->membp);
                    current_section = waitfor->section;
                    close_waitfor(W_SEND2);
                } else {err_msg2(ERROR______EXPECTED,".section", &epoint);goto breakerr;}
                break;
            case CMD_ENDU: /* .endu */
                if (diagnostics.optimize) cpu_opt_invalidate();
                if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                if (waitfor->what==W_ENDU) {
                    if ((waitfor->skip & 1) != 0) {
                        current_section->l_address = current_section->l_unionend;
                        if (current_section->l_address.bank > all_mem) {
                            current_section->l_address.bank &= all_mem;
                            err_msg2(ERROR_ADDRESS_LARGE, NULL, &epoint);
                        }
                        if (current_section->address != current_section->unionend) {
                            current_section->address = current_section->unionend;
                            memjmp(&current_section->mem, current_section->address);
                        }
                        current_section->unionmode = waitfor->breakout;
                        current_section->unionstart = waitfor->addr; current_section->unionend = waitfor->addr2;
                        current_section->l_unionstart = waitfor->laddr; current_section->l_unionend = waitfor->laddr2;
                    }
                    close_waitfor(W_ENDU);
                } else if (close_waitfor(W_ENDU2)) {
                    nobreak = false; 
                    current_section->l_address = current_section->l_unionend;
                    if (current_section->l_address.bank > all_mem) {
                        current_section->l_address.bank &= all_mem;
                        err_msg2(ERROR_ADDRESS_LARGE, NULL, &epoint);
                    }
                    if (current_section->address != current_section->unionend) {
                        current_section->address = current_section->unionend;
                        memjmp(&current_section->mem, current_section->address);
                    }
                } else err_msg2(ERROR______EXPECTED,".union", &epoint);
                break;
            case CMD_ENDP: /* .endp */
                if (diagnostics.optimize) cpu_opt_invalidate();
                if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                if (close_waitfor(W_ENDP)) {
                } else if (waitfor->what==W_ENDP2) {
                    if (((current_section->l_address.address ^ waitfor->laddr.address) & 0xff00) != 0 || 
                            current_section->l_address.bank != waitfor->laddr.bank) {
                        err_msg2(ERROR____PAGE_ERROR, &current_section->l_address, &epoint);
                    }
                    if (waitfor->label != NULL) set_size(waitfor->label, current_section->address - waitfor->addr, &current_section->mem, waitfor->memp, waitfor->membp);
                    close_waitfor(W_ENDP2);
                } else err_msg2(ERROR______EXPECTED,".page", &epoint);
                break;
            case CMD_HERE: /* .here */
                if (diagnostics.optimize) cpu_opt_invalidate();
                if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                if (close_waitfor(W_HERE)) {
                } else if (waitfor->what==W_HERE2) {
                    current_section->l_address.address = (waitfor->laddr.address + current_section->address - waitfor->addr) & 0xffff;
                    if (current_section->address > waitfor->addr) {
                        if (current_section->l_address.address == 0) current_section->l_address.address = 0x10000;
                    }
                    current_section->l_address.bank = (waitfor->laddr.bank + ((current_section->address - waitfor->addr) & ~(address_t)0xffff)) & all_mem;
                    if (current_section->l_address.bank > all_mem) {
                        current_section->l_address.bank &= all_mem;
                        err_msg2(ERROR_ADDRESS_LARGE, NULL, &epoint);
                    }
                    val_destroy(current_section->l_address_val);
                    current_section->l_address_val = waitfor->val; waitfor->val = NULL;
                    if (waitfor->label != NULL) set_size(waitfor->label, current_section->address - waitfor->addr, &current_section->mem, waitfor->memp, waitfor->membp);
                    close_waitfor(W_HERE2);
                    current_section->logicalrecursion--;
                } else err_msg2(ERROR______EXPECTED,".logical", &epoint);
                break;
            case CMD_BEND: /* .bend */
                if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                if (close_waitfor(W_BEND)) {
                } else if (waitfor->what==W_BEND2) {
                    if (waitfor->label != NULL) set_size(waitfor->label, current_section->address - waitfor->addr, &current_section->mem, waitfor->memp, waitfor->membp);
                    if (pop_context()) err_msg2(ERROR______EXPECTED,".block", &epoint);
                    close_waitfor(W_BEND2);
                } else err_msg2(ERROR______EXPECTED,".block", &epoint);
                break;
            case CMD_ENDWEAK: /* .endweak */
                if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                if (close_waitfor(W_WEAK)) {
                    strength--;
                } else if (waitfor->what==W_WEAK2) {
                    if (waitfor->label != NULL) set_size(waitfor->label, current_section->address - waitfor->addr, &current_section->mem, waitfor->memp, waitfor->membp);
                    close_waitfor(W_WEAK2);
                    strength--;
                } else err_msg2(ERROR______EXPECTED,".weak", &epoint);
                break;
            case CMD_END: /* .end */
                if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                nobreak = false;
                break;
            case CMD_TEXT: /* .text */
            case CMD_PTEXT: /* .ptext */
            case CMD_SHIFT: /* .shift */
            case CMD_SHIFTL: /* .shiftl */
            case CMD_NULL: /* .null */
            case CMD_BYTE: /* .byte */
            case CMD_CHAR: /* .char */
            case CMD_RTA: /* .rta */
            case CMD_ADDR: /* .addr */
            case CMD_SINT: /* .sint */
            case CMD_WORD: /* .word */
            case CMD_LINT: /* .lint */
            case CMD_LONG: /* .long */
            case CMD_DINT: /* .dint */
            case CMD_DWORD: /* .dword */
            case CMD_BINARY: if ((waitfor->skip & 1) != 0)
                { /* .binary */
                    address_t uninit = 0, sum = 0;

                    if (diagnostics.optimize) cpu_opt_invalidate();
                    mark_mem(&current_section->mem, current_section->address, star);
                    poke_pos = &epoint;
                    if (prm<CMD_BYTE) {    /* .text .ptext .shift .shiftl .null */
                        int ch2 = -2;
                        struct values_s *vs;
                        if (newlabel != NULL && newlabel->value->obj == CODE_OBJ) {
                            ((Code *)newlabel->value)->dtype = D_BYTE;
                        }
                        if (prm==CMD_PTEXT) ch2=0;
                        if (!get_exp(0, cfile, 0, 0, NULL)) goto breakerr;
                        while ((vs = get_val()) != NULL) {
                            poke_pos = &vs->epoint;
                            if (textrecursion(vs->val, prm, &ch2, &uninit, &sum, SIZE_MAX)) {
                                err_msg_still_none(NULL, poke_pos);
                                if (ch2 < -1)  ch2 = -1;
                            }
                        }
                        if (uninit != 0) {memskip(uninit);sum += uninit;}
                        if (ch2 >= 0) {
                            if (prm==CMD_SHIFT) ch2|=0x80;
                            if (prm==CMD_SHIFTL) ch2|=0x01;
                            pokeb((unsigned int)ch2); sum++;
                        } else if (prm==CMD_SHIFT || prm==CMD_SHIFTL) {
                            if (uninit != 0) {
                                err_msg2(ERROR___NO_LAST_GAP, NULL, poke_pos);
                            } else if (ch2 == -2) {
                                err_msg2(ERROR__BYTES_NEEDED, NULL, &epoint);
                            }
                        }
                        if (prm==CMD_NULL) pokeb(0);
                        if (prm==CMD_PTEXT) {
                            if (sum > 0x100) err_msg2(ERROR____PTEXT_LONG, &sum, &epoint);

                            if (current_section->dooutput) write_mark_mem(&current_section->mem, sum-1);
                        }
                    } else if (prm<=CMD_DWORD) { /* .byte .word .int .rta .long */
                        int bits;
                        struct values_s *vs;
                        if (newlabel != NULL && newlabel->value->obj == CODE_OBJ) {
                            signed char dtype;
                            switch (prm) {
                            case CMD_DINT: dtype = D_DINT;break;
                            case CMD_LINT: dtype = D_LINT;break;
                            case CMD_SINT: dtype = D_SINT;break;
                            case CMD_CHAR: dtype = D_CHAR;break;
                            default:
                            case CMD_BYTE: dtype = D_BYTE;break;
                            case CMD_RTA:
                            case CMD_ADDR:
                            case CMD_WORD: dtype = D_WORD;break;
                            case CMD_LONG: dtype = D_LONG;break;
                            case CMD_DWORD: dtype = D_DWORD;break;
                            }
                            ((Code *)newlabel->value)->dtype = dtype;
                        }
                        switch (prm) {
                        case CMD_CHAR: bits = -8; break;
                        case CMD_SINT: bits = -16; break;
                        case CMD_LINT: bits = -24; break;
                        case CMD_DINT: bits = -32; break;
                        case CMD_BYTE: bits = 8; break;
                        default: bits = 16; break;
                        case CMD_LONG: bits = 24; break;
                        case CMD_DWORD: bits = 32; break;
                        }
                        if (!get_exp(0, cfile, 0, 0, NULL)) goto breakerr;
                        while ((vs = get_val()) != NULL) {
                            poke_pos = &vs->epoint;
                            if (byterecursion(vs->val, prm, &uninit, bits)) err_msg_still_none(NULL, poke_pos);
                        }
                        if (uninit != 0) memskip(uninit);
                    } else if (prm==CMD_BINARY) { /* .binary */
                        char *path = NULL;
                        size_t foffset = 0;
                        size_t fsize = all_mem2 + 1;
                        uval_t uval;
                        struct values_s *vs;
                        str_t filename;

                        if (fsize == 0) fsize = all_mem2; /* overflow */
                        if (newlabel != NULL && newlabel->value->obj == CODE_OBJ) {
                            ((Code *)newlabel->value)->dtype = D_BYTE;
                        }
                        if (!get_exp(0, cfile, 1, 3, &epoint)) goto breakerr;
                        vs = get_val();
                        if (!tostr(vs, &filename)) {
                            path = get_path(&filename, cfile->realname);
                        }
                        if ((vs = get_val()) != NULL) {
                            if (touval2(vs->val, &uval, 8 * sizeof uval, &vs->epoint)) {}
                            else foffset = uval;
                            if ((vs = get_val()) != NULL) {
                                if (touval2(vs->val, &uval, 8 * sizeof uval, &vs->epoint)) {}
                                else fsize = uval;
                            }
                        }

                        if (path != NULL) {
                            struct file_s *cfile2 = openfile(path, cfile->realname, 1, &filename, &epoint);
                            if (cfile2 != NULL) {
                                for (; fsize != 0 && foffset < cfile2->len;fsize--) {
                                    pokeb(cfile2->data[foffset]);foffset++;
                                }
                            }
                            free(path);
                        }
                    }

                    if (nolisting == 0) {
                        list_mem(&current_section->mem, current_section->dooutput);
                    }
                }
                break;
            case CMD_OFFS: if ((waitfor->skip & 1) != 0)
                {   /* .offs */
                    struct values_s *vs;
                    ival_t ival;

                    if (diagnostics.optimize) cpu_opt_invalidate();
                    listing_line(listing, epoint.pos);
                    if (!current_section->moved) {
                        if (current_section->end < current_section->address) current_section->end = current_section->address;
                        current_section->moved = true;
                    }
                    current_section->wrapwarn = false;
                    if (!get_exp(0, cfile, 1, 1, &epoint)) goto breakerr;
                    vs = get_val();
                    if (toival(vs->val, &ival, 8 * sizeof ival, &vs->epoint)) break;
                    if (ival != 0) {
                        if (current_section->structrecursion != 0) {
                            if (ival < 0) err_msg2(ERROR___NOT_ALLOWED, ".offs", &epoint);
                            else if (ival != 0) {
                                poke_pos = &epoint;
                                memskip((uval_t)ival);
                            }
                        } else current_section->address = (address_t)((int)current_section->address + ival);
                        current_section->address &= all_mem2;
                        memjmp(&current_section->mem, current_section->address);
                    }
                }
                break;
            case CMD_LOGICAL: if ((waitfor->skip & 1) != 0)
                { /* .logical */
                    struct values_s *vs;
                    uval_t uval;

                    if (diagnostics.optimize) cpu_opt_invalidate();
                    listing_line(listing, epoint.pos);
                    new_waitfor(W_HERE2, &epoint);waitfor->laddr = current_section->l_address;waitfor->label = newlabel;waitfor->addr = current_section->address;waitfor->memp = newmemp;waitfor->membp = newmembp; waitfor->val = val_reference(current_section->l_address_val);
                    current_section->logicalrecursion++;
                    if (!get_exp(0, cfile, 1, 1, &epoint)) goto breakerr;
                    vs = get_val();
                    if (current_section->structrecursion != 0 && !current_section->dooutput) err_msg2(ERROR___NOT_ALLOWED, ".logical", &epoint);
                    else do {
                        atype_t am;
                        Obj *tmp = vs->val;
                        if (touval(tmp->obj->address(tmp, &am), &uval, all_mem_bits, &vs->epoint)) break;
                        if (am != A_NONE && check_addr(am)) {
                            err_msg_output_and_destroy(err_addressing(am, &vs->epoint));
                            break;
                        }
                        current_section->l_address.address = uval & 0xffff;
                        current_section->l_address.bank = uval & all_mem & ~(address_t)0xffff;
                        val_destroy(current_section->l_address_val);
                        current_section->l_address_val = val_reference(vs->val);
                    } while (false);
                    newlabel = NULL;
                } else new_waitfor(W_HERE, &epoint);
                break;
            case CMD_AS: /* .as */
            case CMD_AL: /* .al */
                if ((waitfor->skip & 1) != 0) {
                    listing_line(listing, epoint.pos);
                    longaccu = (prm == CMD_AL);
                }
                break;
            case CMD_XS: /* .xs */
            case CMD_XL: /* .xl */
                if ((waitfor->skip & 1) != 0) {
                    listing_line(listing, epoint.pos);
                    longindex = (prm == CMD_XL);
                }
                break;
            case CMD_AUTSIZ: /* .autsiz */
            case CMD_MANSIZ: /* .mansiz */
                if ((waitfor->skip & 1) != 0) {
                    listing_line(listing, epoint.pos);
                    autosize = (prm == CMD_AUTSIZ);
                }
                break;
            case CMD_BLOCK: if ((waitfor->skip & 1) != 0)
                { /* .block */
                    listing_line(listing, epoint.pos);
                    new_waitfor(W_BEND2, &epoint);
                    if (newlabel != NULL && newlabel->value->obj == CODE_OBJ) {
                        push_context(((Code *)newlabel->value)->names);
                        waitfor->label = newlabel;waitfor->addr = current_section->address;waitfor->memp = newmemp;waitfor->membp = newmembp;
                        newlabel = NULL;
                    } else {
                        Label *label;
                        bool labelexists;
                        str_t tmpname;
                        if (sizeof(anonident2) != sizeof(anonident2.type) + sizeof(anonident2.padding) + sizeof(anonident2.star_tree) + sizeof(anonident2.vline)) memset(&anonident2, 0, sizeof anonident2);
                        else anonident2.padding[0] = anonident2.padding[1] = anonident2.padding[2] = 0;
                        anonident2.type = '.';
                        anonident2.star_tree = star_tree;
                        anonident2.vline = vline;
                        tmpname.data = (const uint8_t *)&anonident2; tmpname.len = sizeof anonident2;
                        label = new_label(&tmpname, mycontext, strength, &labelexists);
                        if (labelexists) {
                            if (label->defpass == pass) err_msg_double_defined(label, &tmpname, &epoint);
                            label->constant = true;
                            label->owner = true;
                            label->defpass = pass;
                            if (label->value->obj != NAMESPACE_OBJ) {
                                val_destroy(label->value);
                                label->value = (Obj *)new_namespace(cflist, &epoint);
                            }
                        } else {
                            label->constant = true;
                            label->owner = true;
                            label->value = (Obj *)new_namespace(cflist, &epoint);
                            label->file_list = cflist;
                            label->epoint = epoint;
                        }
                        push_context((Namespace *)label->value);
                    }
                } else new_waitfor(W_BEND, &epoint);
                break;
            case CMD_WEAK: if ((waitfor->skip & 1) != 0)
                { /* .weak */
                    listing_line(listing, epoint.pos);
                    new_waitfor(W_WEAK2, &epoint);waitfor->label = newlabel;waitfor->addr = current_section->address;waitfor->memp = newmemp;waitfor->membp = newmembp;
                    strength++;
                    newlabel = NULL;
                } else new_waitfor(W_WEAK, &epoint);
                break;
            case CMD_SEED:
            case CMD_DATABANK:
            case CMD_DPAGE:
            case CMD_EOR: if ((waitfor->skip & 1) != 0)
                { /* .databank, .dpage, .eor */
                    uval_t uval;
                    struct values_s *vs;
                    listing_line(listing, epoint.pos);
                    if (!get_exp(0, cfile, 1, 1, &epoint)) goto breakerr;
                    vs = get_val();
                    switch (prm) {
                    case CMD_DATABANK:
                        if (vs->val == &gap_value->v) databank = 256;
                        else if (touval(vs->val, &uval, 8, &vs->epoint)) {}
                        else databank = uval & 0xff;
                        break;
                    case CMD_DPAGE:
                        if (vs->val == &gap_value->v) dpage = 65536;
                        else if (touval(vs->val, &uval, 16, &vs->epoint)) {}
                        else dpage = uval & 0xffff;
                        break;
                    case CMD_EOR:
                        if (touval(vs->val, &uval, 8, &vs->epoint)) {}
                        else outputeor = uval & 0xff;
                        break;
                    case CMD_SEED:
                        random_reseed(vs->val, &vs->epoint);
                        break;
                    default:
                        break;
                    }
                }
                break;
            case CMD_FILL:
            case CMD_ALIGN: if ((waitfor->skip & 1) != 0)
                { /* .fill, .align */
                    address_t db = 0;
                    uval_t uval;
                    struct values_s *vs;
                    if (diagnostics.optimize) cpu_opt_invalidate();
                    if (newlabel != NULL && newlabel->value->obj == CODE_OBJ) {
                        ((Code *)newlabel->value)->dtype = D_BYTE;
                    }
                    if (!get_exp(0, cfile, 1, 2, &epoint)) goto breakerr;
                    if (prm == CMD_ALIGN) {
                        address_t max = (current_section->logicalrecursion == 0) ? all_mem2 : all_mem;
                        if (current_section->structrecursion != 0 && !current_section->dooutput) err_msg2(ERROR___NOT_ALLOWED, ".align", &epoint);
                        vs = get_val();
                        if (touval2(vs->val, &uval, 8 * sizeof uval, &vs->epoint)) {}
                        else if (uval == 0) err_msg2(ERROR_NO_ZERO_VALUE, NULL, &vs->epoint);
                        else if (uval > 1) {
                            address_t itt = (current_section->logicalrecursion == 0) ? current_section->address : ((current_section->l_address.address + current_section->l_address.bank) & all_mem);
                            if (uval > max) {
                                if (itt != 0) db = max - itt + 1;
                            } else {
                                address_t rem = itt % (address_t)uval;
                                if (rem != 0) db = (address_t)uval - rem;
                            }
                        }
                    } else {
                        vs = get_val();
                        if (touval2(vs->val, &uval, 8 * sizeof uval, &vs->epoint)) {}
                        else db = uval;
                    }
                    mark_mem(&current_section->mem, current_section->address, star);
                    if ((vs = get_val()) != NULL) {
                        address_t uninit = 0, sum = 0;
                        size_t memp, membp;
                        int ch2 = -2;
                        get_mem(&current_section->mem, &memp, &membp);

                        poke_pos = &vs->epoint;
                        if (textrecursion(vs->val, CMD_TEXT, &ch2, &uninit, &sum, db)) {
                            err_msg_still_none(NULL, poke_pos);
                            if (ch2 < -1)  ch2 = -1;
                        }
                        sum += uninit;
                        if (ch2 >= 0 && sum < db) {
                            pokeb((unsigned int)ch2); sum++;
                        }

                        db -= sum;
                        if (db != 0) {
                            if (sum == 1 && ch2 >= 0) {
                                while ((db--) != 0) pokeb((unsigned int)ch2); /* single byte shortcut */
                            } else if (sum == uninit) {
                                if (ch2 == -2) err_msg2(ERROR__BYTES_NEEDED, NULL, poke_pos);
                                uninit += db; /* gap shortcut */
                            } else {
                                size_t offs = 0;
                                while (db != 0) { /* pattern repeat */
                                    int ch;
                                    db--;
                                    ch = read_mem(&current_section->mem, memp, membp, offs);
                                    if (ch < 0) uninit++;
                                    else {
                                        if (uninit != 0) {memskip(uninit); uninit = 0;}
                                        pokeb((unsigned int)ch);
                                    }
                                    offs++;
                                    if (offs >= sum) offs = 0;
                                }
                            }
                        }
                        if (uninit != 0) memskip(uninit);
                    } else if (db != 0) {
                        poke_pos = &epoint;
                        memskip(db);
                    }
                    if (nolisting == 0) {
                        list_mem(&current_section->mem, current_section->dooutput);
                    }
                }
                break;
            case CMD_ASSERT: if ((waitfor->skip & 1) != 0)
                { /* .assert */
                    uval_t uval;
                    struct values_s *vs;
                    listing_line(listing, epoint.pos);
                    if (!get_exp(0, cfile, 3, 3, &epoint)) goto breakerr;
                    vs = get_val();
                    if (touval(vs->val, &uval, 8 * sizeof uval, &vs->epoint)) current_section->provides = ~(uval_t)0;
                    else current_section->provides = uval;
                    vs = get_val();
                    if (touval(vs->val, &uval, 8 * sizeof uval, &vs->epoint)) current_section->requires = 0;
                    else current_section->requires = uval;
                    vs = get_val();
                    if (touval(vs->val, &uval, 8 * sizeof uval, &vs->epoint)) current_section->conflicts = 0;
                    else current_section->conflicts = uval;
                }
                break;
            case CMD_CHECK: if ((waitfor->skip & 1) != 0)
                { /* .check */
                    uval_t uval;
                    struct values_s *vs;
                    listing_line(listing, epoint.pos);
                    if (!get_exp(0, cfile, 2, 2, &epoint)) goto breakerr;
                    vs = get_val();
                    if (touval(vs->val, &uval, 8 * sizeof uval, &vs->epoint)) {}
                    else if ((uval & current_section->provides) != uval) err_msg2(ERROR_REQUIREMENTS_, NULL, &epoint);
                    vs = get_val();
                    if (touval(vs->val, &uval, 8 * sizeof uval, &vs->epoint)) {}
                    else if ((uval & current_section->provides) != 0) err_msg2(ERROR______CONFLICT, NULL, &epoint);
                }
                break;
            case CMD_WARN:
            case CMD_CWARN:
            case CMD_ERROR:
            case CMD_CERROR: if ((waitfor->skip & 1) != 0)
                { /* .warn .cwarn .error .cerror */
                    bool writeit = true;
                    struct values_s *vs;
                    listing_line(listing, epoint.pos);
                    if (!get_exp(0, cfile, (prm == CMD_CWARN || prm == CMD_CERROR) ? 1 : 0, 0, &epoint)) goto breakerr;
                    if (prm == CMD_CWARN || prm == CMD_CERROR) {
                        vs = get_val();
                        if (tobool(vs, &writeit)) writeit = false;
                    }
                    if (writeit) {
                        size_t i, len = get_val_remaining(), len2 = 0, chars = 0;
                        Str *v;
                        Obj **vals;
                        uint8_t *s;
                        Tuple *tmp = new_tuple();
                        tmp->data = vals = list_create_elements(tmp, len);
                        for (i = 0; i < len; i++) {
                            vs = get_val(); val = vs->val;
                            if (val == &none_value->v) val = (Obj *)ref_str(null_str);
                            else {
                                val = STR_OBJ->create(val, &vs->epoint);
                                if (val->obj != STR_OBJ) {
                                    if (val == &none_value->v) err_msg_still_none(NULL, &vs->epoint);
                                    else if (val->obj == ERROR_OBJ) err_msg_output((Error *)val); 
                                    val_destroy(val);
                                    val = (Obj *)ref_str(null_str); 
                                } else {
                                    Str *str = (Str *)val;
                                    len2 += str->len;
                                    if (len2 < str->len) err_msg_out_of_memory(); /* overflow */
                                    chars += str->chars;
                                }
                            }
                            vals[i] = val;
                        }
                        tmp->len = i;
                        v = new_str(len2);
                        v->chars = chars;
                        s = v->data;
                        for (i = 0; i < len; i++) {
                            Str *str = (Str *)vals[i];
                            if (str->len != 0) {
                                memcpy(s, str->data, str->len);
                                s += str->len;
                            }
                        }
                        err_msg2((prm==CMD_CERROR || prm==CMD_ERROR)?ERROR__USER_DEFINED:ERROR_WUSER_DEFINED, v, &epoint);
                        val_destroy(&v->v);
                        val_destroy(&tmp->v);
                    } else while (get_val() != NULL);
                }
                break;
            case CMD_ENC: if ((waitfor->skip & 1) != 0)
                { /* .enc */
                    str_t encname;
                    listing_line(listing, epoint.pos);
                    encname.len = 0;
                    if (pline[lpoint.pos] != '"' && pline[lpoint.pos] != '\'') { /* will be removed to allow variables */
                        if (diagnostics.deprecated) err_msg2(ERROR_______OLD_ENC, NULL, &lpoint);
                        encname.data = pline + lpoint.pos; encname.len = get_label();
                    }
                    if (encname.len == 0) {
                        struct values_s *vs;
                        if (!get_exp(0, cfile, 1, 1, &epoint)) goto breakerr;
                        vs = get_val();
                        if (tostr(vs, &encname)) break;
                        if (encname.len == 0) {err_msg2(ERROR__EMPTY_STRING, NULL, &vs->epoint); break;}
                    }
                    actual_encoding = new_encoding(&encname, &epoint);
                }
                break;
            case CMD_CDEF: if ((waitfor->skip & 1) != 0)
                { /* .cdef */
                    struct trans_s tmp, *t;
                    struct encoding_s *old = actual_encoding;
                    bool rc;
                    size_t len;
                    listing_line(listing, epoint.pos);
                    actual_encoding = NULL;
                    rc = get_exp(0, cfile, 2, 0, &epoint);
                    actual_encoding = old;
                    len = get_val_remaining();
                    if (!rc) goto breakerr;
                    for (;;) {
                        bool endok = false;
                        size_t i;
                        bool tryit = true;
                        uval_t uval;
                        struct values_s *vs;
                        linepos_t opoint;

                        vs = get_val();
                        if (vs == NULL) break;

                        opoint = &vs->epoint;
                        val = vs->val;
                        if (val->obj == STR_OBJ) {
                            Str *str = (Str *)val;
                            if (str->len == 0) {err_msg2(ERROR__EMPTY_STRING, NULL, &vs->epoint); tryit = false;}
                            else {
                                uchar_t ch = str->data[0];
                                if ((ch & 0x80) != 0) i = utf8in(str->data, &ch); else i = 1;
                                tmp.start = ch;
                                if (str->len > i) {
                                    ch = str->data[i];
                                    if ((ch & 0x80) != 0) i += utf8in(str->data + i, &ch); else i++;
                                    tmp.end = ch;
                                    endok = true;
                                    if (str->len > i) {err_msg2(ERROR_NOT_TWO_CHARS, NULL, &vs->epoint); tryit = false;}
                                }
                            }
                        } else {
                            if (touval2(val, &uval, 24, &vs->epoint)) tryit = false;
                            tmp.start = uval;
                        }
                        if (!endok) {
                            vs = get_val();
                            if (vs == NULL) { err_msg_argnum(len, len + 2, 0, &epoint); goto breakerr; }

                            val = vs->val;
                            if (val->obj == STR_OBJ) {
                                Str *str = (Str *)val;
                                if (str->len == 0) {err_msg2(ERROR__EMPTY_STRING, NULL, &vs->epoint); tryit = false;}
                                else {
                                    uchar_t ch = str->data[0];
                                    if ((ch & 0x80) != 0) i = utf8in(str->data, &ch); else i = 1;
                                    tmp.end = ch;
                                    if (str->len > i) {err_msg2(ERROR__NOT_ONE_CHAR, NULL, &vs->epoint); tryit = false;}
                                }
                            } else {
                                if (touval2(val, &uval, 24, &vs->epoint)) tryit = false;
                                tmp.end = uval;
                            }
                        }
                        vs = get_val();
                        if (vs == NULL) { err_msg_argnum(len, len + 1, 0, &epoint); goto breakerr;}
                        if (touval(vs->val, &uval, 8, &vs->epoint)) {}
                        else if (tryit) {
                            tmp.offset = uval & 0xff;
                            if (tmp.start > tmp.end) {
                                uchar_t tmpe = tmp.start;
                                tmp.start = tmp.end;
                                tmp.end = tmpe;
                            }
                            t = new_trans(&tmp, actual_encoding);
                            if (t->start != tmp.start || t->end != tmp.end || t->offset != tmp.offset) {
                                err_msg2(ERROR__DOUBLE_RANGE, NULL, opoint); goto breakerr;
                            }
                        }
                    }
                }
                break;
            case CMD_EDEF: if ((waitfor->skip & 1) != 0)
                { /* .edef */
                    struct encoding_s *old = actual_encoding;
                    bool rc;
                    size_t len;
                    listing_line(listing, epoint.pos);
                    actual_encoding = NULL;
                    rc = get_exp(0, cfile, 2, 0, &epoint);
                    actual_encoding = old;
                    if (!rc) goto breakerr;
                    len = get_val_remaining();
                    for (;;) {
                        struct values_s *vs, *vs2;
                        bool tryit;
                        str_t escape;

                        vs = get_val();
                        if (vs == NULL) break;
                        tryit = !tostr(vs, &escape);

                        if (tryit && escape.len == 0) {
                            err_msg2(ERROR__EMPTY_STRING, NULL, &vs->epoint);
                            tryit = false;
                        }
                        vs2 = get_val();
                        if (vs2 == NULL) { err_msg_argnum(len, len + 1, 0, &epoint); goto breakerr; }
                        val = vs2->val;
                        if (val == &none_value->v) err_msg_still_none(NULL, &vs2->epoint);
                        else if (val->obj == ERROR_OBJ) err_msg_output((Error *)val);
                        else if (tryit && new_escape(&escape, val, actual_encoding, &vs2->epoint)) {
                            err_msg2(ERROR_DOUBLE_ESCAPE, NULL, &vs->epoint); goto breakerr;
                        }
                    }
                }
                break;
            case CMD_CPU: if ((waitfor->skip & 1) != 0)
                { /* .cpu */
                    struct values_s *vs;
                    const struct cpu_list_s {
                        const char *name;
                        const struct cpu_s *def;
                    } *cpui;
                    static const struct cpu_list_s cpus[] = {
                        {"6502", &c6502}, {"65c02", &c65c02},
                        {"65ce02", &c65ce02}, {"6502i", &c6502i},
                        {"65816", &w65816}, {"65dtv02", &c65dtv02},
                        {"65el02", &c65el02}, {"r65c02", &r65c02},
                        {"w65c02", &w65c02}, {"4510", &c4510},
                        {"default", NULL}, {NULL, NULL},
                    };
                    str_t cpuname;

                    if (diagnostics.optimize) cpu_opt_invalidate();
                    listing_line(listing, epoint.pos);
                    if (!get_exp(0, cfile, 1, 1, &epoint)) goto breakerr;
                    vs = get_val();
                    if (tostr(vs, &cpuname)) break;
                    for (cpui = cpus; cpui->name != NULL; cpui++) {
                        if (cpuname.len == strlen(cpui->name) && memcmp(cpui->name, cpuname.data, cpuname.len) == 0) {
                            const struct cpu_s *cpumode = (cpui->def != NULL) ? cpui->def : arguments.cpumode;
                            if (current_section->l_address.bank > cpumode->max_address) {
                                current_section->l_address.bank &= cpumode->max_address;
                                err_msg2(ERROR_ADDRESS_LARGE, NULL, &epoint);
                            }
                            set_cpumode(cpumode);
                            break;
                        }
                    }
                    if (cpui->name == NULL) err_msg2(ERROR___UNKNOWN_CPU, &cpuname, &vs->epoint);
                }
                break;
            case CMD_REPT: if ((waitfor->skip & 1) != 0)
                { /* .rept */
                    uval_t cnt;
                    struct values_s *vs;
                    Obj *nf;
                    if (diagnostics.optimize) cpu_opt_invalidate();
                    listing_line(listing, epoint.pos);
                    new_waitfor(W_NEXT, &epoint);waitfor->skip = 0;
                    if (!get_exp(0, cfile, 1, 1, &epoint)) goto breakerr;
                    vs = get_val();
                    if (touval2(vs->val, &cnt, 8 * sizeof cnt, &vs->epoint)) break;
                    if (cnt > 0) {
                        line_t lin = lpoint.line;
                        bool labelexists;
                        struct star_s *s = new_star(vline, &labelexists);
                        struct avltree *stree_old = star_tree;
                        line_t ovline = vline, lvline;

                        if (labelexists && s->addr != star) {
                            if (fixeddig && pass > max_pass) err_msg_cant_calculate(NULL, &epoint);
                            fixeddig = false;
                        }
                        s->addr = star;
                        star_tree = &s->tree;vline = 0;
                        new_waitfor(W_NEXT2, &epoint);
                        waitfor->breakout = false;
                        for (;;) {
                            lpoint.line = lin;
                            waitfor->skip = 1; lvline = vline;
                            nf = compile(cflist);
                            if (nf == NULL || waitfor->breakout || (--cnt) == 0) {
                                break;
                            }
                            if ((waitfor->skip & 1) != 0) listing_line_cut(listing, waitfor->epoint.pos);
                        }
                        if (nf != NULL) {
                            if ((waitfor->skip & 1) != 0) listing_line(listing, waitfor->epoint.pos);
                            else listing_line_cut2(listing, waitfor->epoint.pos);
                        }
                        close_waitfor(W_NEXT2);
                        if (nf != NULL) close_waitfor(W_NEXT);
                        star_tree = stree_old; vline = ovline + vline - lvline;
                    }
                } else new_waitfor(W_NEXT, &epoint);
                break;
            case CMD_PRON: /* .pron */
                if ((waitfor->skip & 1) != 0) {
                    listing_line(listing, epoint.pos);
                    if (nolisting != 0) nolisting--;
                }
                break;
            case CMD_PROFF: /* .proff */
                if ((waitfor->skip & 1) != 0) {
                    nolisting++;
                    listing_line(listing, epoint.pos);
                }
                break;
            case CMD_SHOWMAC: /* .showmac */
            case CMD_HIDEMAC: /* .hidemac */
                if ((waitfor->skip & 1) != 0) {
                    listing_line(listing, epoint.pos);
                    if (diagnostics.ignored) err_msg2(ERROR_DIRECTIVE_IGN, NULL, &epoint);
                }
                break;
            case CMD_COMMENT: /* .comment */
                if ((waitfor->skip & 1) != 0) listing_line(listing, epoint.pos);
                new_waitfor(W_ENDC, &epoint);
                waitfor->skip = 0;
                break;
            case CMD_INCLUDE:
            case CMD_BINCLUDE: if ((waitfor->skip & 1) != 0)
                { /* .include, .binclude */
                    struct file_s *f = NULL;
                    struct values_s *vs;
                    char *path;
                    str_t filename;
                    if (diagnostics.optimize) cpu_opt_invalidate();
                    listing_line(listing, epoint.pos);
                    if (!get_exp(0, cfile, 1, 1, &epoint)) goto breakerr;
                    vs = get_val();
                    if (!tostr(vs, &filename)) {
                        path = get_path(&filename, cfile->realname);
                        f = openfile(path, cfile->realname, 2, &filename, &epoint);
                        free(path);
                    }
                    if (here() != 0 && here() != ';') err_msg(ERROR_EXTRA_CHAR_OL,NULL);

                    if (f == NULL) goto breakerr;
                    if (f->open>1) {
                        err_msg2(ERROR_FILERECURSION, NULL, &epoint);
                    } else {
                        bool starexists;
                        struct star_s *s = new_star(vline, &starexists);
                        struct avltree *stree_old = star_tree;
                        uint32_t old_backr = backr, old_forwr = forwr;
                        line_t lin = lpoint.line;
                        line_t vlin = vline;
                        struct file_list_s *cflist2;


                        if (starexists && s->addr != star) {
                            if (fixeddig && pass > max_pass) err_msg_cant_calculate(NULL, &epoint);
                            fixeddig = false;
                        }
                        s->addr = star;
                        cflist2 = enterfile(f, &epoint);
                        lpoint.line = vline = 0;
                        star_tree = &s->tree;
                        backr = forwr = 1;
                        reffile = f->uid;
                        listing_file(listing, ";******  Processing file: ", f->realname);
                        if (prm == CMD_BINCLUDE) {
                            if (newlabel != NULL && newlabel->value->obj == CODE_OBJ) {
                                push_context(((Code *)newlabel->value)->names);
                            } else {
                                Label *label;
                                bool labelexists;
                                str_t tmpname;
                                if (sizeof(anonident2) != sizeof(anonident2.type) + sizeof(anonident2.padding) + sizeof(anonident2.star_tree) + sizeof(anonident2.vline)) memset(&anonident2, 0, sizeof anonident2);
                                else anonident2.padding[0] = anonident2.padding[1] = anonident2.padding[2] = 0;
                                anonident2.type = '.';
                                anonident2.star_tree = star_tree;
                                anonident2.vline = vline;
                                tmpname.data = (const uint8_t *)&anonident2; tmpname.len = sizeof anonident2;
                                label = new_label(&tmpname, mycontext, strength, &labelexists);
                                if (labelexists) {
                                    if (label->defpass == pass) err_msg_double_defined(label, &tmpname, &epoint);
                                    label->constant = true;
                                    label->owner = true;
                                    label->defpass = pass;
                                    if (label->value->obj != NAMESPACE_OBJ) {
                                        val_destroy(label->value);
                                        label->value = (Obj *)new_namespace(cflist, &epoint);
                                    }
                                } else {
                                    label->constant = true;
                                    label->owner = true;
                                    label->value = (Obj *)new_namespace(cflist, &epoint);
                                    label->file_list = cflist;
                                    label->epoint = epoint;
                                }
                                push_context((Namespace *)label->value);
                            }
                            val = compile(cflist2);
                            pop_context();
                        } else val = compile(cflist2);
                        if (val != NULL) val_destroy(val);
                        lpoint.line = lin; vline = vlin;
                        star_tree = stree_old;
                        backr = old_backr; forwr = old_forwr;
                        exitfile();
                        reffile = cfile->uid;
                        listing_file(listing, ";******  Return to file: ", cfile->realname);
                    }
                    closefile(f);
                    goto breakerr;
                }
                break;
            case CMD_FOR: if ((waitfor->skip & 1) != 0)
                { /* .for */
                    line_t lin, xlin;
                    struct linepos_s apoint, bpoint = {0, 0};
                    int nopos = -1;
                    struct oper_s tmp;
                    struct linepos_s epoint2, epoint3;
                    uint8_t *expr;
                    Label *label;
                    Obj *nf = NULL;
                    struct star_s *s;
                    struct avltree *stree_old;
                    line_t ovline, lvline;
                    bool starexists;
                    size_t lentmp;

                    if (diagnostics.optimize) cpu_opt_invalidate();
                    listing_line(listing, epoint.pos);
                    new_waitfor(W_NEXT, &epoint);waitfor->skip = 0;
                    { /* label */
                        bool labelexists;
                        str_t varname;
                        epoint = lpoint;
                        varname.data = pline + lpoint.pos; varname.len = get_label();
                        if (varname.len != 0) {
                            struct linepos_s epoints[3];
                            if (varname.len > 1 && varname.data[0] == '_' && varname.data[1] == '_') {err_msg2(ERROR_RESERVED_LABL, &varname, &epoint); goto breakerr;}
                            ignore(); wht = here();
                            if (wht != '=') {
                                if (wht == ':' && pline[lpoint.pos + 1] == '=' && !arguments.tasmcomp) lpoint.pos++;
                                else {err_msg(ERROR______EXPECTED, "'='");goto breakerr;}
                            }
                            lpoint.pos++;
                            if (!get_exp(1, cfile, 1, 1, &lpoint)) goto breakerr;
                            val = get_vals_addrlist(epoints);
                            if (val->obj == ERROR_OBJ) {err_msg_output((Error *)val); val = (Obj *)none_value;}
                            label = new_label(&varname, (varname.data[0] == '_') ? cheap_context : current_context, strength, &labelexists);
                            if (labelexists) {
                                if (label->constant) {
                                    err_msg_double_defined(label, &varname, &epoint);
                                    val_destroy(val);
                                } else {
                                    label->owner = false;
                                    label->file_list = cflist;
                                    label->epoint = epoint;
                                    if (label->defpass != pass) {
                                        label->ref = false;
                                        label->defpass = pass;
                                    }
                                    val_destroy(label->value);
                                    label->value = val;
                                }
                            } else {
                                label->constant = false;
                                label->owner = false;
                                label->value = val;
                                label->file_list = cflist;
                                label->epoint = epoint;
                            }
                            ignore();
                        }
                    }
                    if (here() != ',') {err_msg(ERROR______EXPECTED, "','"); goto breakerr;}
                    lpoint.pos++;ignore();

                    s = new_star(vline, &starexists); stree_old = star_tree; ovline = vline;
                    if (starexists && s->addr != star) {
                        if (fixeddig && pass > max_pass) err_msg_cant_calculate(NULL, &epoint);
                        fixeddig = false;
                    }
                    s->addr = star;
                    star_tree = &s->tree; lvline = vline = 0;
                    xlin = lin = lpoint.line; apoint = lpoint;
                    lentmp = strlen((const char *)pline) + 1;
                    expr = (uint8_t *)mallocx(lentmp);
                    memcpy(expr, pline, lentmp); label = NULL;
                    new_waitfor(W_NEXT2, &epoint);
                    waitfor->breakout = false;
                    tmp.op = NULL;
                    for (;;) {
                        lpoint = apoint;
                        if (here() != ',' && here() != 0) {
                            struct values_s *vs;
                            bool truth;
                            if (!get_exp(1, cfile, 1, 1, &apoint)) break;
                            vs = get_val();
                            if (tobool(vs, &truth)) break;
                            if (!truth) break;
                        }
                        if (nopos < 0) {
                            str_t varname;
                            ignore();if (here() != ',') {err_msg(ERROR______EXPECTED, "','"); break;}
                            lpoint.pos++;ignore();
                            if (here() == 0 || here() == ';') {bpoint.pos = 0; nopos = 0;}
                            else {
                                Namespace *context;
                                bool labelexists;
                                epoint = lpoint;
                                varname.data = pline + lpoint.pos; varname.len = get_label();
                                if (varname.len == 0) {err_msg2(ERROR_LABEL_REQUIRE, NULL, &epoint);break;}
                                if (varname.len > 1 && varname.data[0] == '_' && varname.data[1] == '_') {err_msg2(ERROR_RESERVED_LABL, &varname, &epoint); goto breakerr;}
                                ignore(); wht = here();
                                while (wht != 0 && !arguments.tasmcomp) {
                                    int wht2 = pline[lpoint.pos + 1];
                                    if (wht2 == '=') {
                                        if (wht == ':') {
                                            wht = '=';
                                            lpoint.pos++;
                                            break;
                                        }
                                        tmp.op = oper_from_token(wht);
                                        if (tmp.op == NULL) break;
                                        epoint3 = lpoint;
                                        lpoint.pos += 2;
                                    } else if (wht2 != 0 && pline[lpoint.pos + 2] == '=') {
                                        tmp.op = oper_from_token2(wht, wht2);
                                        if (tmp.op == NULL) break;
                                        epoint3 = lpoint;
                                        lpoint.pos += 3;
                                    } else break;

                                    ignore();
                                    epoint2 = lpoint;
                                    tmp.epoint = &epoint;
                                    tmp.epoint2 = &epoint2;
                                    tmp.epoint3 = &epoint3;
                                    break;
                                }
                                context = (varname.data[0] == '_') ? cheap_context : current_context;
                                if (tmp.op == NULL) {
                                    if (wht != '=') {err_msg(ERROR______EXPECTED, "'='"); break;}
                                    lpoint.pos++;ignore();
                                    label = new_label(&varname, context, strength, &labelexists);
                                    if (labelexists) {
                                        if (label->constant) { err_msg_double_defined(label, &varname, &epoint); break; }
                                        label->owner = false;
                                        label->file_list = cflist;
                                        label->epoint = epoint;
                                        if (label->defpass != pass) {
                                            label->ref = false;
                                            label->defpass = pass;
                                        }
                                    } else {
                                        label->constant = false;
                                        label->owner = false;
                                        label->value = (Obj *)ref_none();
                                        label->file_list = cflist;
                                        label->epoint = epoint;
                                    }
                                } else {
                                    label = find_label2(&varname, context);
                                    if (label == NULL) {err_msg_not_defined2(&varname, context, false, &epoint); break;}
                                    if (label->constant) {err_msg_not_variable(label, &varname, &epoint); break;}
                                    if (diagnostics.case_symbol && str_cmp(&varname, &label->name) != 0) err_msg_symbol_case(&varname, label, &epoint);
                                }
                                bpoint = lpoint; nopos = 1;
                            }
                        } else {
                            if ((waitfor->skip & 1) != 0) listing_line_cut(listing, waitfor->epoint.pos);
                        }
                        waitfor->skip = 1;lvline = vline;
                        nf = compile(cflist);
                        xlin = lpoint.line;
                        pline = expr;
                        lpoint.line = lin;
                        if (nf == NULL || waitfor->breakout) break;
                        if (nopos > 0) {
                            struct linepos_s epoints[3];
                            lpoint = bpoint;
                            if (!get_exp(0, cfile, 0, 0, &bpoint)) break;
                            val = get_vals_addrlist(epoints);
                            if (tmp.op != NULL) {
                                Obj *result2, *val1 = label->value;
                                tmp.v1 = val1;
                                tmp.v2 = val;
                                result2 = tmp.v1->obj->calc2(&tmp);
                                if (result2->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)result2); result2 = (Obj *)ref_none(); }
                                else if (tmp.op == &o_MIN || tmp.op == &o_MAX) {
                                    if (result2 == &true_value->v) val_replace(&result2, val1);
                                    else if (result2 == &false_value->v) val_replace(&result2, val);
                                }
                                val_destroy(val);
                                val = result2;
                            }
                            val_destroy(label->value);
                            label->value = val;
                        }
                    }
                    if (nf != NULL) {
                        if ((waitfor->skip & 1) != 0) listing_line(listing, waitfor->epoint.pos);
                        else listing_line_cut2(listing, waitfor->epoint.pos);
                    }
                    close_waitfor(W_NEXT2);
                    free(expr);
                    if (nf != NULL) close_waitfor(W_NEXT);
                    lpoint.line = xlin;
                    star_tree = stree_old; vline = ovline + vline - lvline;
                    goto breakerr;
                } else new_waitfor(W_NEXT, &epoint);
                break;
            case CMD_CONTINUE:
            case CMD_BREAK: if ((waitfor->skip & 1) != 0)
                { /* .continue, .break */
                    size_t wp = waitfor_p + 1;
                    bool nok = true;
                    listing_line(listing, epoint.pos);
                    while ((wp--) != 0) {
                        if (waitfors[wp].what == W_NEXT2) {
                            if (wp != 0 && prm == CMD_BREAK) waitfors[wp].breakout = true;
                            for (;wp <= waitfor_p; wp++) waitfors[wp].skip = 0;
                            nok = false;
                            break;
                        }
                    }
                    if (nok) err_msg2(ERROR______EXPECTED,".for or .rept", &epoint);
                }
                break;
            case CMD_PAGE: if ((waitfor->skip & 1) != 0)
                { /* .page */
                    if (diagnostics.optimize) cpu_opt_invalidate();
                    listing_line(listing, epoint.pos);
                    new_waitfor(W_ENDP2, &epoint);waitfor->addr = current_section->address;waitfor->laddr = current_section->l_address;waitfor->label = newlabel;waitfor->memp = newmemp;waitfor->membp = newmembp;
                    newlabel = NULL;
                } else new_waitfor(W_ENDP, &epoint);
                break;
            case CMD_OPTION: if ((waitfor->skip & 1) != 0)
                { /* .option */
                    static const str_t branch_across = {24, (const uint8_t *)"allow_branch_across_page"};
                    static const str_t longjmp = {22, (const uint8_t *)"auto_longbranch_as_jmp"};
                    struct values_s *vs;
                    str_t optname, cf;
                    listing_line(listing, epoint.pos);
                    optname.data = pline + lpoint.pos; optname.len = get_label();
                    if (optname.len == 0) { err_msg2(ERROR_LABEL_REQUIRE, NULL, &epoint); goto breakerr;}
                    ignore();if (here() != '=') {err_msg(ERROR______EXPECTED, "'='"); goto breakerr;}
                    epoint = lpoint;
                    lpoint.pos++;
                    if (!get_exp(0, cfile, 1, 0, &epoint)) goto breakerr;
                    vs = get_val();
                    str_cfcpy(&cf, &optname);
                    if (str_cmp(&cf, &branch_across) == 0) {
                        if (tobool(vs, &allowslowbranch)) break;
                    } else if (str_cmp(&cf, &longjmp) == 0) {
                        if (tobool(vs, &longbranchasjmp)) break;
                    } else err_msg2(ERROR_UNKNOWN_OPTIO, &optname, &epoint);
                }
                break;
            case CMD_GOTO: if ((waitfor->skip & 1) != 0)
                { /* .goto */
                    bool noerr = true;
                    struct values_s *vs;
                    Lbl *lbl;
                    listing_line(listing, epoint.pos);
                    if (!get_exp(0, cfile, 1, 1, &epoint)) goto breakerr;
                    if (!arguments.tasmcomp && diagnostics.deprecated) err_msg2(ERROR______OLD_GOTO, NULL, &epoint);
                    vs = get_val(); val = vs->val;
                    if (val->obj == ERROR_OBJ) {err_msg_output((Error *)val); break; }
                    if (val == &none_value->v) {err_msg_still_none(NULL, &vs->epoint); break; }
                    if (val->obj != LBL_OBJ) {err_msg_wrong_type(val, LBL_OBJ, &vs->epoint); break;}
                    lbl = (Lbl *)val;
                    if (lbl->file_list == cflist && lbl->parent == current_context) {
                        while (lbl->waitforp < waitfor_p) {
                            const char *msg = NULL;
                            switch (waitfor->what) {
                            case W_SWITCH2:
                            case W_SWITCH: msg = ".endswitch"; break;
                            case W_WEAK2:
                            case W_WEAK: msg = ".endweak"; break;
                            case W_ENDM2:
                            case W_ENDM: msg = ".endm"; break;
                            case W_ENDF2:
                            case W_ENDF: msg = ".endf"; break;
                            case W_NEXT: msg = ".next"; break;
                            case W_PEND: msg = ".pend"; break;
                            case W_BEND2:
                            case W_BEND: msg = ".bend"; break;
                            case W_ENDS2:
                            case W_ENDS: msg = ".ends"; break;
                            case W_SEND2:
                            case W_SEND: msg = ".send"; break;
                            case W_ENDU2:
                            case W_ENDU: msg = ".endu"; break;
                            case W_ENDP2:
                            case W_ENDP: msg = ".endp"; break;
                            case W_HERE2:
                            case W_HERE: msg = ".here"; break;
                            case W_ENDC: msg = ".endc"; break;
                            case W_NONE:
                            case W_NEXT2:
                            case W_FI:
                            case W_FI2: break;
                            }
                            if (msg != NULL) {
                                err_msg2(ERROR______EXPECTED, msg, &waitfor->epoint);
                                noerr = false;
                            }
                            close_waitfor(waitfor->what);
                        }
                        if (noerr) {
                            lpoint.line = lbl->sline;
                        }
                    } else err_msg_not_defined(NULL, &vs->epoint);
                }
                break;
            case CMD_MACRO:
            case CMD_SEGMENT: /* .macro, .segment */
                if ((waitfor->skip & 1) != 0) {
                    listing_line(listing, 0);
                    if (labelname.len == 0) err_msg2(ERROR_LABEL_REQUIRE, NULL, &epoint);
                }
                new_waitfor(W_ENDM, &epoint);
                waitfor->skip = 0;
                break;
            case CMD_FUNCTION: /* .function */
                if ((waitfor->skip & 1) != 0) {
                    listing_line(listing, 0);
                    if (labelname.len == 0) err_msg2(ERROR_LABEL_REQUIRE, NULL, &epoint);
                }
                new_waitfor(W_ENDF, &epoint);
                waitfor->skip = 0;
                break;
            case CMD_VAR: /* .var */
                if ((waitfor->skip & 1) != 0) {
                    listing_line(listing, 0);
                    if (labelname.len == 0) err_msg2(ERROR_LABEL_REQUIRE, NULL, &epoint);
                    goto breakerr;
                }
                break;
            case CMD_LBL: /* .lbl */
                if ((waitfor->skip & 1) != 0) {
                    listing_line(listing, 0);
                    if (labelname.len == 0) err_msg2(ERROR_LABEL_REQUIRE, NULL, &epoint);
                    else if (!arguments.tasmcomp && diagnostics.deprecated) err_msg2(ERROR______OLD_GOTO, NULL, &epoint);
                }
                break;
            case CMD_PROC: /* .proc */
                if ((waitfor->skip & 1) != 0) {
                    listing_line(listing, 0);
                    if (labelname.len == 0) err_msg2(ERROR_LABEL_REQUIRE, NULL, &epoint);
                }
                new_waitfor(W_PEND, &epoint);
                waitfor->skip = 0;waitfor->label = NULL;
                break;
            case CMD_STRUCT: /* .struct */
                new_waitfor(W_ENDS, &epoint);
                if ((waitfor->skip & 1) != 0) {
                    listing_line(listing, 0);
                    waitfor->breakout = current_section->unionmode;
                    current_section->unionmode = false;
                }
                break;
            case CMD_UNION: /* .union */
                new_waitfor(W_ENDU, &epoint);
                if ((waitfor->skip & 1) != 0) {
                    if (diagnostics.optimize) cpu_opt_invalidate();
                    listing_line(listing, 0);
                    waitfor->breakout = current_section->unionmode;
                    waitfor->addr = current_section->unionstart; waitfor->addr2 = current_section->unionend;
                    waitfor->laddr = current_section->l_unionstart; waitfor->laddr2 = current_section->l_unionend;
                    current_section->unionmode = true;
                    current_section->unionstart = current_section->unionend = current_section->address;
                    current_section->l_unionstart = current_section->l_unionend = current_section->l_address;
                }
                break;
            case CMD_DSTRUCT: if ((waitfor->skip & 1) != 0)
                { /* .dstruct */
                    bool old_unionmode = current_section->unionmode;
                    struct values_s *vs;
                    if (diagnostics.optimize) cpu_opt_invalidate();
                    listing_line(listing, 0);
                    current_section->unionmode = false;
                    if (!get_exp(1, cfile, 1, 0, &epoint)) goto breakerr;
                    vs = get_val(); val = vs->val;
                    if (val->obj == ERROR_OBJ) {err_msg_output((Error *)val); goto breakerr; }
                    if (val == &none_value->v) {err_msg_still_none(NULL, &vs->epoint); goto breakerr; }
                    ignore();if (here() == ',') lpoint.pos++;
                    if (val->obj != STRUCT_OBJ) {err_msg_wrong_type(val, STRUCT_OBJ, &vs->epoint); goto breakerr;}
                    current_section->structrecursion++;
                    val = macro_recurse(W_ENDS2, val, NULL, &epoint);
                    if (val != NULL) val_destroy(val);
                    current_section->structrecursion--;
                    current_section->unionmode = old_unionmode;
                }
                goto breakerr;
            case CMD_DUNION: if ((waitfor->skip & 1) != 0)
                { /* .dunion */
                    bool old_unionmode = current_section->unionmode;
                    struct values_s *vs;
                    address_t old_unionstart = current_section->unionstart, old_unionend = current_section->unionend;
                    if (diagnostics.optimize) cpu_opt_invalidate();
                    listing_line(listing, 0);
                    current_section->unionmode = true;
                    current_section->unionstart = current_section->unionend = current_section->address;
                    if (!get_exp(1, cfile, 1, 0, &epoint)) goto breakerr;
                    vs = get_val(); val = vs->val;
                    if (val->obj == ERROR_OBJ) {err_msg_output((Error *)val); goto breakerr; }
                    if (val == &none_value->v) {err_msg_still_none(NULL, &vs->epoint); goto breakerr; }
                    ignore();if (here() == ',') lpoint.pos++;
                    if (val->obj != UNION_OBJ) {err_msg_wrong_type(val, UNION_OBJ, &vs->epoint); goto breakerr;}
                    current_section->structrecursion++;
                    val = macro_recurse(W_ENDU2, val, NULL, &epoint);
                    if (val != NULL) val_destroy(val);
                    current_section->structrecursion--;
                    current_section->unionmode = old_unionmode;
                    current_section->unionstart = old_unionstart; current_section->unionend = old_unionend;
                }
                goto breakerr;
            case CMD_DSECTION: if ((waitfor->skip & 1) != 0)
                { /* .dsection */
                    struct section_s *tmp3;
                    str_t sectionname;

                    if (diagnostics.optimize) cpu_opt_invalidate();
                    listing_line(listing, epoint.pos);
                    if (current_section->structrecursion != 0 || !current_section->dooutput) { err_msg2(ERROR___NOT_ALLOWED, ".dsection", &epoint); goto breakerr; }
                    epoint = lpoint;
                    sectionname.data = pline + lpoint.pos; sectionname.len = get_label();
                    if (sectionname.len == 0) {err_msg2(ERROR_LABEL_REQUIRE, NULL, &epoint); goto breakerr;}
                    tmp3=new_section(&sectionname);
                    if (tmp3->defpass == pass) {
                        err_msg_double_definedo(tmp3->file_list, &tmp3->epoint, &sectionname, &epoint);
                    } else {
                        address_t t;
                        if (tmp3->usepass == 0 || tmp3->defpass < pass - 1) {
                            tmp3->wrapwarn = tmp3->moved = false;
                            tmp3->end = tmp3->start = tmp3->restart = tmp3->address = current_section->address;
                            tmp3->l_restart = tmp3->l_address = current_section->l_address;
                            tmp3->usepass = pass;
                            restart_memblocks(&tmp3->mem, tmp3->address);
                            if (diagnostics.optimize) cpu_opt_invalidate();
                            if (fixeddig && pass > max_pass) err_msg_cant_calculate(NULL, &epoint);
                            fixeddig = false;
                        }
                        tmp3->provides = ~(uval_t)0;tmp3->requires = tmp3->conflicts = 0;
                        tmp3->dooutput = current_section->dooutput;
                        tmp3->unionmode = current_section->unionmode;
                        tmp3->unionstart = current_section->unionstart;
                        tmp3->unionend = current_section->unionend;
                        tmp3->l_unionstart = current_section->l_unionstart;
                        tmp3->l_unionend = current_section->l_unionend;
                        tmp3->structrecursion = current_section->structrecursion;
                        tmp3->logicalrecursion = current_section->logicalrecursion;
                        val_destroy(tmp3->l_address_val); /* TODO: restart as well */
                        tmp3->l_address_val = val_reference(current_section->l_address_val);
                        tmp3->file_list = cflist;
                        tmp3->epoint = epoint;
                        if (tmp3->usepass == pass) {
                            t = tmp3->size;
                            if (t < tmp3->end - tmp3->start) t = tmp3->end - tmp3->start;
                            if (!tmp3->moved) {
                                if (t < tmp3->address - tmp3->start) t = tmp3->address - tmp3->start;
                            }
                            tmp3->restart = current_section->address;
                            if (tmp3->l_restart.address != current_section->l_address.address ||
                                    tmp3->l_restart.bank != current_section->l_address.bank) {
                                tmp3->l_restart = current_section->l_address;
                                if (fixeddig && pass > max_pass) err_msg_cant_calculate(NULL, &epoint);
                                fixeddig = false;
                            }
                        } else {
                            if (!tmp3->moved) {
                                if (tmp3->end < tmp3->address) tmp3->end = tmp3->address;
                                tmp3->moved = true;
                            }
                            tmp3->wrapwarn = false;
                            t = tmp3->end - tmp3->start;
                            tmp3->end = tmp3->start = tmp3->restart = tmp3->address = current_section->address;
                            tmp3->l_address = current_section->l_address;
                            if (tmp3->l_restart.address != current_section->l_address.address ||
                                    tmp3->l_restart.bank != current_section->l_address.bank) {
                                tmp3->l_restart = current_section->l_address;
                                if (fixeddig && pass > max_pass) err_msg_cant_calculate(NULL, &epoint);
                                fixeddig = false;
                            }
                            tmp3->size = t;
                            restart_memblocks(&tmp3->mem, tmp3->address);
                            if (diagnostics.optimize) cpu_opt_invalidate();
                        }
                        tmp3->usepass = pass;
                        tmp3->defpass = pass;
                        if (t != 0) {
                            poke_pos = &epoint;
                            memskip(t);
                        }
                        memref(&current_section->mem, &tmp3->mem);
                    }
                }
                break;
            case CMD_SECTION: if ((waitfor->skip & 1) != 0)
                { /* .section */
                    struct section_s *tmp;
                    str_t sectionname;
                    listing_line(listing, 0);
                    new_waitfor(W_SEND, &epoint);waitfor->section = current_section;
                    epoint = lpoint;
                    sectionname.data = pline + lpoint.pos; sectionname.len = get_label();
                    if (sectionname.len == 0) {err_msg2(ERROR_LABEL_REQUIRE, NULL, &epoint); goto breakerr;}
                    if (current_section->structrecursion != 0 || !current_section->dooutput) {err_msg2(ERROR___NOT_ALLOWED, ".section", &epoint); goto breakerr;}
                    tmp = find_new_section(&sectionname);
                    if (tmp->usepass == 0 || tmp->defpass < pass - 1) {
                        tmp->end = tmp->start = tmp->restart = tmp->address = 0;
                        tmp->size = tmp->l_restart.address = tmp->l_restart.bank = tmp->l_address.address = tmp->l_address.bank = 0;
                        if (tmp->usepass != 0 && tmp->usepass >= pass - 1) err_msg_not_defined(&sectionname, &epoint);
                        else if (fixeddig && pass > max_pass) err_msg_cant_calculate(&sectionname, &epoint);
                        fixeddig = false;
                        tmp->defpass = pass - 1;
                        restart_memblocks(&tmp->mem, tmp->address);
                        if (diagnostics.optimize) cpu_opt_invalidate();
                    } else if (tmp->usepass != pass) {
                        if (!tmp->moved) {
                            if (tmp->end < tmp->address) tmp->end = tmp->address;
                            tmp->moved = true;
                        }
                        tmp->size = tmp->end - tmp->start;
                        tmp->end = tmp->start = tmp->restart;
                        tmp->wrapwarn = false;
                        tmp->address = tmp->restart;
                        tmp->l_address = tmp->l_restart;
                        restart_memblocks(&tmp->mem, tmp->address);
                        if (diagnostics.optimize) cpu_opt_invalidate();
                    }
                    tmp->usepass = pass;
                    waitfor->what = W_SEND2;
                    current_section = tmp;
                } else new_waitfor(W_SEND, &epoint);
                break;
            case lenof(command):
                if ((waitfor->skip & 1) != 0) goto as_macro2;
                break;
            default:
                if ((waitfor->skip & 1) != 0) {
                    listing_line(listing, epoint.pos);
                    err_msg(ERROR_GENERL_SYNTAX,NULL);
                    goto breakerr;
                }
                break;
            }
            break;
        case '#':if ((waitfor->skip & 1) != 0) /* skip things if needed */
            {                   /* macro stuff */
                struct values_s *vs;
                lpoint.pos++;
            as_macro2:
                if (!get_exp(2, cfile, 1, 1, &epoint)) goto breakerr;
                vs = get_val(); val = vs->val;
                switch (val->obj->type) {
                case T_ERROR: err_msg_output((Error *)val); goto breakerr;
                case T_NONE: err_msg_still_none(NULL, &vs->epoint); goto breakerr;
                default : err_msg_wrong_type(val, NULL, &vs->epoint); goto breakerr;
                case T_MACRO:
                case T_SEGMENT:
                case T_MFUNC: break;
                }
            as_macro:
                listing_line_cut(listing, epoint.pos);
                if (val->obj == MACRO_OBJ) {
                    Namespace *context;
                    if (newlabel != NULL && !((Macro *)val)->retval && newlabel->value->obj == CODE_OBJ) {
                        context = ((Code *)newlabel->value)->names;
                    } else {
                        Label *label;
                        bool labelexists;
                        str_t tmpname;
                        if (sizeof(anonident2) != sizeof(anonident2.type) + sizeof(anonident2.padding) + sizeof(anonident2.star_tree) + sizeof(anonident2.vline)) memset(&anonident2, 0, sizeof anonident2);
                        else anonident2.padding[0] = anonident2.padding[1] = anonident2.padding[2] = 0;
                        anonident2.type = '#';
                        anonident2.star_tree = star_tree;
                        anonident2.vline = vline;
                        tmpname.data = (const uint8_t *)&anonident2; tmpname.len = sizeof anonident2;
                        label = new_label(&tmpname, mycontext, strength, &labelexists);
                        if (labelexists) {
                            if (label->defpass == pass) err_msg_double_defined(label, &tmpname, &epoint);
                            label->constant = true;
                            label->owner = true;
                            label->defpass = pass;
                            if (label->value->obj != NAMESPACE_OBJ) {
                                val_destroy(label->value);
                                label->value = (Obj *)new_namespace(cflist, &epoint);
                            }
                        } else {
                            label->constant = true;
                            label->owner = true;
                            label->value = (Obj *)new_namespace(cflist, &epoint);
                            label->file_list = cflist;
                            label->epoint = epoint;
                        } 
                        context = (Namespace *)label->value;
                    }
                    val = macro_recurse(W_ENDM2, val, context, &epoint);
                } else if (val->obj == MFUNC_OBJ) {
                    Label *label;
                    Mfunc *mfunc;
                    bool labelexists;
                    str_t tmpname;
                    if (sizeof(anonident2) != sizeof(anonident2.type) + sizeof(anonident2.padding) + sizeof(anonident2.star_tree) + sizeof(anonident2.vline)) memset(&anonident2, 0, sizeof anonident2);
                    else anonident2.padding[0] = anonident2.padding[1] = anonident2.padding[2] = 0;
                    anonident2.type = '#';
                    anonident2.star_tree = star_tree;
                    anonident2.vline = vline;
                    tmpname.data = (const uint8_t *)&anonident2; tmpname.len = sizeof anonident2;
                    label = new_label(&tmpname, ((Mfunc *)val)->namespaces[((Mfunc *)val)->nslen - 1], strength, &labelexists);
                    if (labelexists) {
                        if (label->defpass == pass) err_msg_double_defined(label, &tmpname, &epoint);
                        label->constant = true;
                        label->owner = true;
                        label->defpass = pass;
                        if (label->value->obj != NAMESPACE_OBJ) {
                            val_destroy(label->value);
                            label->value = (Obj *)new_namespace(cflist, &epoint);
                        }
                    } else {
                        label->constant = true;
                        label->owner = true;
                        label->value = (Obj *)new_namespace(cflist, &epoint);
                        label->file_list = cflist;
                        label->epoint = epoint;
                    }
                    mfunc = (Mfunc *)val_reference(val);
                    if (!get_exp(4, cfile, 0, 0, NULL)) {
                        val = NULL;
                        val_destroy(&mfunc->v);
                        goto breakerr;
                    }
                    val = mfunc_recurse(W_ENDF2, mfunc, (Namespace *)label->value, &epoint, strength);
                    val_destroy(&mfunc->v);
                } else val = macro_recurse(W_ENDM2, val, NULL, &epoint);
                if (val != NULL) {
                    if (newlabel != NULL) {
                        newlabel->update_after = true;
                        const_assign(newlabel, val);
                    } else val_destroy(val);
                }
                break;
            }
            break;
        default:
            if ((waitfor->skip & 1) != 0) {
                str_t opname;
                bool down;

                if (newlabel != NULL && newlabel->value->obj == CODE_OBJ && labelname.len != 0 && labelname.data[0] != '_' && labelname.data[0] != '+' && labelname.data[0] != '-') {val_destroy(&cheap_context->v);cheap_context = ref_namespace(((Code *)newlabel->value)->names);}
                opname.data = pline + lpoint.pos; opname.len = get_label();
                if (opname.len == 3 && (prm = lookup_opcode(opname.data)) >= 0) {
                    Error *err;
                    struct linepos_s oldlpoint;
                    struct linepos_s epoints[3];
                    unsigned int w;
                    if (false) {
                as_opcode:
                        opname = labelname;
                    }
                    ignore();
                    oldlpoint = lpoint;
                    w = 3; /* 0=byte 1=word 2=long 3=negative/too big */
                    if (here() == 0 || here() == ';') {val = (Obj *)ref_addrlist(null_addrlist);}
                    else {
                        if (arguments.tasmcomp) {
                            if (here() == '!') {w = 1; lpoint.pos++;}
                        } else {
                            if (here()=='@') {
                                switch (pline[lpoint.pos + 1] | arguments.caseinsensitive) {
                                case 'b': w = 0;break;
                                case 'w': w = 1;break;
                                case 'l': w = 2;break;
                                default:err_msg2(ERROR______EXPECTED, "@b or @w or @l", &lpoint);goto breakerr;
                                }
                                lpoint.pos += 2;
                            }
                        }
                        if (!get_exp(3, cfile, 0, 0, NULL)) goto breakerr;
                        val = get_vals_addrlist(epoints);
                    }
                    if (val->obj == TUPLE_OBJ || val->obj == LIST_OBJ) {
                        epoints[1] = epoints[0];
                        epoints[2] = epoints[0];
                        if (!instrecursion((List *)val, prm, w, &epoint, epoints)) {
                            listing_instr(listing, 0, 0, -1);
                        }
                        err = NULL;
                    } else err = instruction(prm, w, val, &epoint, epoints);
                    val_destroy(val);
                    if (err == NULL) {
                        if (diagnostics.alias && cpu->mnemonic[prm] != cpu->mnemonic[cpu->alias[prm]]) err_msg_alias(cpu->mnemonic[prm], cpu->mnemonic[cpu->alias[prm]], &epoint);
                        break;
                    }
                    tmp2 = find_label(&opname, NULL);
                    if (tmp2 != NULL) {
                        Type *obj = tmp2->value->obj;
                        if (diagnostics.case_symbol && str_cmp(&opname, &tmp2->name) != 0) err_msg_symbol_case(&opname, tmp2, &epoint);
                        if (obj == MACRO_OBJ || obj == SEGMENT_OBJ || obj == MFUNC_OBJ) {
                            val_destroy(&err->v);
                            tmp2->shadowcheck = true;
                            lpoint = oldlpoint;
                            val = tmp2->value;
                            goto as_macro;
                        }
                    }
                    err_msg_output_and_destroy(err);
                    break;
                }
                down = (opname.data[0] != '_');
                tmp2 = down ? find_label(&opname, NULL) : find_label2(&opname, cheap_context);
                if (tmp2 != NULL) {
                    Type *obj = tmp2->value->obj;
                    if (diagnostics.case_symbol && str_cmp(&opname, &tmp2->name) != 0) err_msg_symbol_case(&opname, tmp2, &epoint);
                    if (obj == MACRO_OBJ || obj == SEGMENT_OBJ || obj == MFUNC_OBJ) {
                        if (down) tmp2->shadowcheck = true;
                        val = tmp2->value;goto as_macro;
                    }
                }
                err_msg2(ERROR_GENERL_SYNTAX, NULL, &epoint);
                goto breakerr;
            }
        }
    finish:
        ignore();if (here() != 0 && here() != ';' && (waitfor->skip & 1) != 0) err_msg(ERROR_EXTRA_CHAR_OL,NULL);
    breakerr:
        if (newlabel != NULL && !newlabel->update_after) set_size(newlabel, current_section->address - oaddr, &current_section->mem, newmemp, newmembp);
    }

    while (oldwaitforp < waitfor_p) {
        const char *msg = NULL;
        switch (waitfor->what) {
        case W_FI2:
        case W_FI: msg = ".fi"; break;
        case W_SWITCH2:
        case W_SWITCH: msg = ".endswitch"; break;
        case W_WEAK2:
        case W_WEAK: msg = ".endweak"; break;
        case W_ENDP2:
        case W_ENDP: msg = ".endp"; break;
        case W_ENDM2:
        case W_ENDM: msg = ".endm"; break;
        case W_ENDF2:
        case W_ENDF: msg = ".endf"; break;
        case W_NEXT: msg = ".next"; break;
        case W_PEND: msg = ".pend"; break;
        case W_BEND2:
        case W_BEND: msg = ".bend"; break;
        case W_ENDC: msg = ".endc"; break;
        case W_ENDS2:
        case W_ENDS: msg = ".ends"; break;
        case W_SEND2:
        case W_SEND: msg = ".send"; break;
        case W_ENDU2:
        case W_ENDU: msg = ".endu"; break;
        case W_HERE2:
        case W_HERE: msg = ".here"; break;
        case W_NEXT2:
        case W_NONE: break;
        }
        if (msg != NULL) err_msg2(ERROR______EXPECTED, msg, &waitfor->epoint);
        close_waitfor(waitfor->what);
    }
    return retval;
}

static int main2(int *argc2, char **argv2[]) {
    size_t j;
    int opts, i;
    struct file_s *fin, *cfile;
    struct file_list_s *cflist;
    static const str_t none_enc = {4, (const uint8_t *)"none"};
    struct linepos_s nopoint = {0, 0};
    char **argv;
    int argc;

    err_init(*argv2[0]);
    objects_init();
    init_section();
    init_file();
    init_variables();
    init_eval();
    init_ternary();
    init_opt_bit();

    fin = openfile(NULL, "", 0, NULL, &nopoint);
    opts = testarg(argc2, argv2, fin); argc = *argc2; argv = *argv2;
    if (opts <= 0) {
        tfree();
        free_macro();
        free(waitfors);
        return (opts < 0) ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    init_encoding(arguments.to_ascii);

    if (arguments.quiet) {
        puts("64tass Turbo Assembler Macro V" VERSION "\n"
             "64TASS comes with ABSOLUTELY NO WARRANTY; This is free software, and you\n"
             "are welcome to redistribute it under certain conditions; See LICENSE!\n");
    }

    /* assemble the input file(s) */
    do {
        if (pass++>max_pass) {err_msg(ERROR_TOO_MANY_PASS, NULL);break;}
        listing_pccolumn = false; fixeddig = true;constcreated = false;error_reset();random_reseed(&int_value[0]->v, NULL);
        restart_memblocks(&root_section.mem, 0);
        if (diagnostics.optimize) cpu_opt_invalidate();
        for (i = opts - 1; i < argc; i++) {
            Obj *val;
            set_cpumode(arguments.cpumode); if (pass == 1 && i == opts - 1) constcreated = false;
            star = databank = dpage = strength = 0;longaccu = longindex = autosize = false;actual_encoding = new_encoding(&none_enc, &nopoint);
            allowslowbranch = true;temporary_label_branch = 0;
            reset_waitfor();lpoint.line = vline = 0;outputeor = 0;forwr = backr = 1;
            reset_context();
            current_section = &root_section;
            reset_section(current_section);
            init_macro();
            /*	nolisting = 0;flist = stderr;*/
            if (i == opts - 1) {
                if (fin->lines != 0) {
                    cflist = enterfile(fin, &nopoint);
                    star_tree = &fin->star;
                    reffile = fin->uid;
                    val = compile(cflist);
                    if (val != NULL) val_destroy(val);
                    exitfile();
                }
                restart_memblocks(&root_section.mem, 0);
                if (diagnostics.optimize) cpu_opt_invalidate();
                continue;
            }
            cfile = openfile(argv[i], "", 0, NULL, &nopoint);
            if (cfile != NULL) {
                cflist = enterfile(cfile, &nopoint);
                star_tree = &cfile->star;
                reffile = cfile->uid;
                val = compile(cflist);
                if (val != NULL) val_destroy(val);
                closefile(cfile);
                exitfile();
            }
        }
        /*garbage_collect();*/
    } while (!fixeddig || constcreated);
    if (arguments.list == NULL) {
        if (diagnostics.shadow) shadow_check(root_namespace);
        if (diagnostics.unused.macro || diagnostics.unused.consts || diagnostics.unused.label || diagnostics.unused.variable) unused_check(root_namespace);
    }
    if (error_serious()) {status();return EXIT_FAILURE;}

    /* assemble again to create listing */
    if (arguments.list != NULL) {
        nolisting = 0;

        max_pass = pass; pass++;
        fixeddig = true;constcreated = false;error_reset();random_reseed(&int_value[0]->v, NULL);
        restart_memblocks(&root_section.mem, 0);
        if (diagnostics.optimize) cpu_opt_invalidate();
        listing = listing_open(arguments.list, argc, argv);
        for (i = opts - 1; i < argc; i++) {
            Obj *val;
            set_cpumode(arguments.cpumode);
            star = databank = dpage = strength = 0;longaccu = longindex = autosize = false;actual_encoding = new_encoding(&none_enc, &nopoint);
            allowslowbranch = true;temporary_label_branch = 0;
            reset_waitfor();lpoint.line = vline = 0;outputeor = 0;forwr = backr = 1;
            reset_context();
            current_section = &root_section;
            reset_section(current_section);
            init_macro();

            if (i == opts - 1) {
                if (fin->lines != 0) {
                    cflist = enterfile(fin, &nopoint);
                    star_tree = &fin->star;
                    reffile = fin->uid;
                    listing_file(listing, ";******  Command line definitions", NULL);
                    val = compile(cflist);
                    if (val != NULL) val_destroy(val);
                    exitfile();
                }
                restart_memblocks(&root_section.mem, 0);
                if (diagnostics.optimize) cpu_opt_invalidate();
                continue;
            }

            cfile = openfile(argv[i], "", 0, NULL, &nopoint);
            if (cfile != NULL) {
                cflist = enterfile(cfile, &nopoint);
                star_tree = &cfile->star;
                reffile = cfile->uid;
                listing_file(listing, ";******  Processing input file: ", argv[i]);
                val = compile(cflist);
                if (val != NULL) val_destroy(val);
                closefile(cfile);
                exitfile();
            }
        }
        /*garbage_collect();*/
        listing_close(listing);
        listing = NULL;

        if (diagnostics.shadow) shadow_check(root_namespace);
        if (diagnostics.unused.macro || diagnostics.unused.consts || diagnostics.unused.label || diagnostics.unused.variable) unused_check(root_namespace);
    }

    set_cpumode(arguments.cpumode);

    for (j = 0; j < arguments.symbol_output_len; j++) {
        const struct symbol_output_s *s = &arguments.symbol_output[j];
        size_t k;
        for (k = 0; k < j; k++) {
            const struct symbol_output_s *s2 = &arguments.symbol_output[k];
            if (strcmp(s->name, s2->name) == 0) break;
        }
        if (labelprint(s, k != j)) break;
    }
    if (arguments.make != NULL) makefile(argc - opts, argv + opts);

    if (error_serious()) {status();return EXIT_FAILURE;}

    output_mem(&root_section.mem, &arguments.output);

    {
        bool e = error_serious();
        status();
        return e ? EXIT_FAILURE : EXIT_SUCCESS;
    }
}

#ifdef _WIN32
static UINT oldcodepage;
static UINT oldcodepage2;

void myexit(void) {
    SetConsoleCP(oldcodepage2);
    SetConsoleOutputCP(oldcodepage);
}

int wmain(int argc, wchar_t *argv2[]) {
    int i, r;
    char **argv;

    if (IsValidCodePage(CP_UTF8)) {
        oldcodepage = GetConsoleOutputCP();
        oldcodepage2 = GetConsoleCP();
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
        atexit(myexit);
    }

    argv = (char **)malloc((argc < 1 ? 1 : argc) * sizeof *argv);
    if (argv == NULL) err_msg_out_of_memory2();
    for (i = 0; i < argc; i++) {
	uchar_t c = 0, lastchar;
	wchar_t *p = argv2[i];
	uint8_t *c2;

	while (*p != 0) p++;
	c2 = (uint8_t *)malloc((p - argv2[i]) * 4 / (sizeof *p) + 1);
	if (c2 == 0) err_msg_out_of_memory2();
	p = argv2[i];
	argv[i] = (char *)c2;

	while (*p != 0) {
	    lastchar = c;
	    c = *p++;
	    if (c >= 0xd800 && c < 0xdc00) {
		if (lastchar < 0xd800 || lastchar >= 0xdc00) continue;
		c = 0xfffd;
	    } else if (c >= 0xdc00 && c < 0xe000) {
		if (lastchar >= 0xd800 && lastchar < 0xdc00) {
		    c ^= 0x360dc00 ^ (lastchar << 10);
		    c += 0x10000;
		} else
		    c = 0xfffd;
	    } else if (lastchar >= 0xd800 && lastchar < 0xdc00) {
		c = 0xfffd;
	    }
	    if (c != 0 && c < 0x80) *c2++ = c; else c2 = utf8out(c, c2);
	}
	*c2++ = 0;
	argv[i] = (char *)realloc(argv[i], (char *)c2 - argv[i]);
	if (argv[i] == NULL) err_msg_out_of_memory2();
    }
    if (argc < 1) {
        argv[0] = (char *)malloc(7);
        if (argv[0] == NULL) err_msg_out_of_memory2();
        strcpy(argv[0], "64tass");
        argc = 1;
    }
    r = main2(&argc, &argv);

    for (i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    return r;
}
#else
int main(int argc, char *argv[]) {
    int i, r;
    char **uargv;

    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");

    uargv = (char **)malloc((argc < 1 ? 1 : (unsigned int)argc) * sizeof *uargv);
    if (uargv == NULL) err_msg_out_of_memory2();
    for (i = 0; i < argc; i++) {
        const char *s = argv[i];
        mbstate_t ps;
        uint8_t *p;
        size_t n = strlen(s), j = 0;
        size_t len = n + 64;
        uint8_t *data = (uint8_t *)malloc(len);
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
            l = (ssize_t)mbrtowc(&w, s + j, n - j,  &ps);
            if (l < 1) {
                w = (uint8_t)s[j];
                if (w == 0 || l == 0) break;
                l = 1;
            }
            j += (size_t)l;
            ch = (uchar_t)w;
            if (ch != 0 && ch < 0x80) *p++ = (uint8_t)ch; else p = utf8out(ch, p);
        }
        *p++ = 0;
        uargv[i] = (char *)data;
    }
    if (argc < 1) {
        argv[0] = (char *)malloc(7);
        if (argv[0] == NULL) err_msg_out_of_memory2();
        memcpy(argv[0], "64tass", 7);
        argc = 1;
    }
    r = main2(&argc, &uargv);

    for (i = 0; i < argc; i++) free(uargv[i]);
    free(uargv);
    return r;
}
#endif


#ifdef __MINGW32__

#include <shellapi.h>

int main(void)
{
  LPWSTR commandLine = GetCommandLineW();
  int argcw = 0;
  LPWSTR *argvw = CommandLineToArgvW(commandLine, &argcw);
  if (!argvw)
    return EXIT_FAILURE;

  int result = wmain(argcw, argvw);
  LocalFree(argvw);
  return result;
}
#endif /* __MINGW32__ */
