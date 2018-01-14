/*
    $Id: error.c 1515 2017-05-01 11:52:39Z soci $

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
#include "error.h"
#include <string.h>
#include <errno.h>
#include "wchar.h"
#include "wctype.h"
#ifdef _WIN32
#include <locale.h>
#endif
#include "file.h"
#include "64tass.h"
#include "macro.h"
#include "unicode.h"
#include "eval.h"
#include "arguments.h"

#include "strobj.h"
#include "addressobj.h"
#include "registerobj.h"
#include "namespaceobj.h"
#include "operobj.h"
#include "typeobj.h"
#include "labelobj.h"
#include "errorobj.h"

#ifdef COLOR_OUTPUT
bool print_use_color = false;
bool print_use_bold = false;
#endif

#define ALIGN(v) (((v) + (sizeof(int *) - 1)) & ~(sizeof(int *) - 1))

static unsigned int errors = 0, warnings = 0;

static struct file_list_s file_list;
static const struct file_list_s *included_from = &file_list;
static struct file_list_s *current_file_list = &file_list;
static const char *prgname;

struct errorbuffer_s {
    size_t max;
    size_t len;
    size_t header_pos;
    uint8_t *data;
};

static struct errorbuffer_s error_list = {0, 0, 0, NULL};
static struct avltree notdefines;

typedef enum Severity_types {
    SV_NOTE, SV_WARNING, SV_NONEERROR, SV_ERROR, SV_FATAL
} Severity_types;

struct errorentry_s {
    Severity_types severity;
    size_t error_len;
    size_t line_len;
    const struct file_list_s *file_list;
    struct linepos_s epoint;
};

struct notdefines_s {
    str_t cfname;
    const struct file_list_s *file_list;
    struct linepos_s epoint;
    uint8_t pass;
    struct avltree_node node;
};

static bool check_duplicate(const struct errorentry_s *nerr) {
    size_t pos;
    const struct errorentry_s *err;
    for (pos = 0; pos < error_list.header_pos; pos = ALIGN(pos + (sizeof *err) + err->line_len + err->error_len)) {
        err = (const struct errorentry_s *)&error_list.data[pos];
        if (err->severity != nerr->severity) continue;
        if (err->file_list != nerr->file_list) continue;
        if (err->line_len != nerr->line_len) continue;
        if (err->error_len != nerr->error_len) continue;
        if (err->epoint.line != nerr->epoint.line) continue;
        if (err->epoint.pos != nerr->epoint.pos) continue;
        if (memcmp(err + 1, nerr + 1, err->line_len + err->error_len) != 0) continue;
        return true;
    }
    return false;
}

static void close_error(void) {
    static bool duplicate;
    if (error_list.header_pos < error_list.len) {
        struct errorentry_s *err = (struct errorentry_s *)&error_list.data[error_list.header_pos];
        err->error_len = error_list.len - error_list.header_pos - (sizeof *err) - err->line_len;
        switch (err->severity) {
        case SV_NOTE: break;
        default: duplicate = check_duplicate(err);
        }
        if (duplicate) {
            error_list.len = error_list.header_pos;
        }
    }
}

static void new_error_msg(Severity_types severity, const struct file_list_s *flist, linepos_t epoint) {
    struct errorentry_s *err;
    size_t line_len;
    close_error();
    switch (severity) {
    case SV_NOTE: line_len = 0; break;
    default: line_len = ((epoint->line == lpoint.line) && in_macro()) ? (strlen((const char *)pline) + 1) : 0; break;
    }
    error_list.header_pos = ALIGN(error_list.len);
    error_list.len = error_list.header_pos + sizeof *err;
    if (error_list.len < sizeof *err) err_msg_out_of_memory2(); /* overflow */
    error_list.len += line_len;
    if (error_list.len < line_len) err_msg_out_of_memory2(); /* overflow */
    if (error_list.len > error_list.max) {
        error_list.max = error_list.len + 0x200;
        if (error_list.max < 0x200) err_msg_out_of_memory2(); /* overflow */
        error_list.data = (uint8_t *)realloc(error_list.data, error_list.max);
        if (error_list.data == NULL) err_msg_out_of_memory2();
    }
    err = (struct errorentry_s *)&error_list.data[error_list.header_pos];
    err->severity = severity;
    err->error_len = 0;
    err->line_len = line_len;
    err->file_list = flist;
    err->epoint = *epoint;
    if (line_len != 0) memcpy(&error_list.data[error_list.header_pos + sizeof *err], pline, line_len);
}

static void new_error_msg2(bool type, linepos_t epoint) {
    new_error_msg(type ? SV_ERROR : SV_WARNING, current_file_list, epoint);
}

static int file_list_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct file_list_s *a = cavltree_container_of(aa, struct file_list_s, node);
    const struct file_list_s *b = cavltree_container_of(bb, struct file_list_s, node);
    if (a->epoint.line != b->epoint.line) return a->epoint.line > b->epoint.line ? 1 : -1;
    if (a->epoint.pos != b->epoint.pos) return a->epoint.pos > b->epoint.pos ? 1 : -1;
    return a->file->uid - b->file->uid;
}

static void file_list_free(struct avltree_node *aa)
{
    struct file_list_s *a = avltree_container_of(aa, struct file_list_s, node);
    avltree_destroy(&a->members, file_list_free);
    free(a);
}

static struct file_list_s *lastfl = NULL;
struct file_list_s *enterfile(struct file_s *file, linepos_t epoint) {
    struct avltree_node *b;
    if (lastfl == NULL) {
        lastfl = (struct file_list_s *)mallocx(sizeof *lastfl);
    }
    lastfl->file = file;
    lastfl->epoint = *epoint;

    b = avltree_insert(&lastfl->node, &current_file_list->members, file_list_compare);
    if (b == NULL) {
        lastfl->parent = current_file_list;
        avltree_init(&lastfl->members);
        current_file_list = lastfl;
        lastfl = NULL;
    } else {
        current_file_list = avltree_container_of(b, struct file_list_s, node);
    }
    curfile = (current_file_list->file != NULL) ? current_file_list->file->uid : 1;
    return current_file_list;
}

void exitfile(void) {
    if (current_file_list->parent != NULL) current_file_list = current_file_list->parent;
    curfile = (current_file_list->file != NULL) ? current_file_list->file->uid : 1;
}

static void adderror2(const uint8_t *s, size_t len) {
    if (len + error_list.len > error_list.max) {
        error_list.max += (len > 0x200) ? len : 0x200;
        error_list.data = (uint8_t *)realloc(error_list.data, error_list.max);
        if (error_list.data == NULL) err_msg_out_of_memory2();
    }
    memcpy(error_list.data + error_list.len, s, len);
    error_list.len += len;
}

static void adderror(const char *s) {
    adderror2((const uint8_t *)s, strlen(s));
}

static const char * const terr_warning[] = {
    "deprecated modulo operator, use '%' instead",
    "deprecated not equal operator, use '!=' instead",
    "deprecated directive, only for TASM compatible mode",
    "please use format(\"%d\", ...) as '^' will change it's meaning",
    "please use quotes now to allow expressions in future",
    "constant result, possibly changeable to 'lda'",
    "independent result, possibly changeable to 'lda'",
#ifdef _WIN32
    "the file's real name is not '",
#endif
#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __MSDOS__ || defined __DOS__
    "use '/' as path separation '",
#else
    "this name uses reserved characters '",
#endif
    "use relative path for '"
};

static const char * const terr_error[] = {
    "double defined range",
    "double defined escape",
    "extra characters on line",
    "more than two characters",
    "floating point overflow",
    "address not in processor address space",
    "general syntax",
    "expression syntax",
    "label required",
    "missing argument",
    "division by zero",
    "zero value not allowed",
    "most significiant bit must be clear in byte",
    "at least one byte is needed",
    "last byte must not be gap",
    "address in different program bank",
    "address out of section",
    "negative number raised on fractional power",
    "square root of negative number",
    "logarithm of non-positive number",
    "not in range -1.0 to 1.0",
    "empty range not allowed",
    "empty string not allowed",
    "more than a single character",
    "too early to reference",
    "requirements not met",
    "conflict",
    "index out of range",
    "key error",
    "offset out of range",
    "not hashable",
    "not a key and value pair",
    "too large for a %u bit signed integer",
    "too large for a %u bit unsigned integer",
    "value needs to be non-negative",
    "operands could not be broadcast together with shapes %" PRIuSIZE " and %" PRIuSIZE,
    "can't get sign of type",
    "can't get absolute value of type",
    "can't get integer value of type",
    "can't get length of type",
    "can't get size of type",
    "can't get boolean value of type",
    "not iterable",
    "no byte sized addressing mode for opcode",
    "no word sized addressing mode for opcode",
    "no long sized addressing mode for opcode",
    "not a direct page address",
    "not a data bank address",
    "not a bank 0 address",
    "out of memory",
    "addressing mode too complex",
    "empty encoding, add something or correct name"
};

static const char * const terr_fatal[] = {
    "can't open file",
    "error reading file",
    "can't write object file",
    "can't write listing file",
    "can't write label file",
    "can't write make file",
    "can't write error file",
    "file recursion",
    "macro recursion too deep",
    "function recursion too deep",
    "too many passes"
};

static void err_msg_variable(Obj *val, linepos_t epoint) {
    Obj *err = val->obj->repr(val, epoint, 40);
    if (err != NULL) {
        if (err->obj == STR_OBJ) {
            Str *str = (Str *)err;
            adderror(" '");
            adderror2(str->data, str->len);
            adderror("'");
        } else if (err->obj == ERROR_OBJ) err_msg_output((Error *)err);
        val_destroy(err);
    }
}

static void str_name(const uint8_t *data, size_t len) {
    adderror(" '");
    if (len != 0) {
        if (data[0] == '-') {
            adderror("-");
        } else if (data[0] == '+') {
            adderror("+");
        } else if (data[0] == '.' || data[0] == '#') {
            adderror("<anonymous>");
        } else adderror2(data, len);
    }
    adderror("'");
}

void err_msg2(Error_types no, const void *prm, linepos_t epoint) {

    if (no < 0x40) {
        switch (no) {
        case ERROR___OPTIMIZABLE:
            new_error_msg2(diagnostic_errors.optimize, epoint);
            adderror("could be shorter by using '");
            adderror((const char *)prm);
            adderror("' instead");
            adderror(" [-Woptimize]");
            break;
        case ERROR______SIMPLIFY:
            new_error_msg2(diagnostic_errors.optimize, epoint);
            adderror("could be simpler by using '");
            adderror((const char *)prm);
            adderror("' instead");
            adderror(" [-Woptimize]");
            break;
        case ERROR_____REDUNDANT:
            new_error_msg2(diagnostic_errors.optimize, epoint);
            adderror("possibly redundant ");
            adderror((const char *)prm);
            adderror(" [-Woptimize]");
            break;
        case ERROR__CONST_RESULT:
            new_error_msg2(diagnostic_errors.optimize, epoint);
            adderror(terr_warning[no]);
            adderror(" [-Woptimize]");
            break;
        case ERROR_______OLD_NEQ:
        case ERROR____OLD_MODULO:
        case ERROR____OLD_STRING:
        case ERROR_______OLD_ENC:
            new_error_msg2(diagnostic_errors.deprecated, epoint);
            adderror(terr_warning[no]);
            adderror(" [-Wdeprecated]");
            break;
        case ERROR_____OLD_EQUAL:
            new_error_msg2(diagnostic_errors.old_equal, epoint);
            adderror("deprecated equal operator, use '==' instead [-Wold-equal]");
            break;
        case ERROR_DUPLICATECASE:
            new_error_msg2(diagnostic_errors.switch_case, epoint);
            adderror("case ignored, value already handled [-Wswitch-case]");
            break;
        case ERROR_NONIMMEDCONST:
            new_error_msg2(diagnostic_errors.immediate, epoint);
            adderror("immediate addressing mode suggested [-Wimmediate]");
            break;
        case ERROR_LEADING_ZEROS:
            new_error_msg2(diagnostic_errors.immediate, epoint);
            adderror("leading zeros ignored [-Wleading-zeros]");
            break;
        case ERROR_DIRECTIVE_IGN:
            new_error_msg2(diagnostic_errors.ignored, epoint);
            adderror("directive ignored [-Wignored]");
            break;
        case ERROR_FLOAT_COMPARE:
            new_error_msg2(diagnostic_errors.float_compare, epoint);
            adderror("approximate floating point ");
            adderror((const char *)prm);
            adderror("' [-Wfloat-compare]");
            break;
        case ERROR___LONG_BRANCH:
            new_error_msg2(diagnostic_errors.long_branch, epoint);
            adderror("long branch used [-Wlong-branch]");
            break;
        case ERROR_WUSER_DEFINED: 
            new_error_msg(SV_WARNING, current_file_list, epoint);
            adderror2(((const Str *)prm)->data, ((const Str *)prm)->len);
            break;
#ifdef _WIN32
        case ERROR___INSENSITIVE:
#endif
#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __MSDOS__ || defined __DOS__
        case ERROR_____BACKSLASH:
#else
        case ERROR__RESERVED_CHR:
#endif
        case ERROR_ABSOLUTE_PATH:
            new_error_msg2(diagnostic_errors.portable, epoint);
            adderror(terr_warning[no]);
            adderror2(((const str_t *)prm)->data, ((const str_t *)prm)->len);
            adderror("' [-Wportable]");
            break;
        default: 
            new_error_msg(SV_WARNING, current_file_list, epoint);
            adderror(terr_warning[no]);
            break;
        }
        return;
    }

    if (no < 0xc0) {
        char line[1024];
        new_error_msg(SV_ERROR, current_file_list, epoint);
        switch (no) {
        case ERROR____PAGE_ERROR:
            adderror("page error at $");
            sprintf(line,"%06" PRIaddress, *(const address_t *)prm); adderror(line);
            break;
        case ERROR_BRANCH_TOOFAR:
            sprintf(line,"branch too far by %+d bytes", *(const int *)prm); adderror(line);
            break;
        case ERROR____PTEXT_LONG:
            sprintf(line,"ptext too long by %" PRIuSIZE " bytes", *(const size_t *)prm - 0x100); adderror(line);
            break;
        case ERROR__BRANCH_CROSS:
            sprintf(line,"branch crosses page by %+d bytes", *(const int *)prm); adderror(line);
            break;
        case ERROR__USER_DEFINED:
            adderror2(((const Str *)prm)->data, ((const Str *)prm)->len);
            break;
        case ERROR______EXPECTED:
            adderror((const char *)prm);
            adderror(" expected");
            break;
        case ERROR___NOT_ALLOWED:
            adderror("not allowed here: ");
            adderror((const char *)prm);
            break;
        case ERROR_RESERVED_LABL:
            adderror("reserved symbol name '");
            adderror2(((const str_t *)prm)->data, ((const str_t *)prm)->len);
            adderror("'");
            break;
        case ERROR_____NOT_BANK0:
        case ERROR____NOT_DIRECT:
        case ERROR__NOT_DATABANK:
        case ERROR_ADDRESS_LARGE:
        case ERROR_CANT_CROSS_BA:
            adderror(terr_error[no - 0x40]);
            if (prm != NULL) err_msg_variable((Obj *)prm, epoint);
            break;
        case ERROR___UNKNOWN_CPU:
            adderror("unknown processor '");
            adderror2(((const str_t *)prm)->data, ((const str_t *)prm)->len);
            adderror("'");
            break;
        default:
            adderror(terr_error[no - 0x40]);
        }
        return;
    }

    new_error_msg(SV_FATAL, current_file_list, epoint);
    switch (no) {
    case ERROR_UNKNOWN_OPTIO:
        adderror("unknown option '");
        adderror2(((const str_t *)prm)->data, ((const str_t *)prm)->len);
        adderror("'");
        break;
    case ERROR____LABEL_ROOT:
        adderror("scope '");
        adderror2(((const str_t *)prm)->data, ((const str_t *)prm)->len);
        adderror("' for label listing not found");
        break;
    default:
        adderror(terr_fatal[no - 0xc0]);
    }
}

void err_msg(Error_types no, const void* prm) {
    err_msg2(no, prm, &lpoint);
}

static void err_msg_str_name(const char *msg, const str_t *name, linepos_t epoint) {
    new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror(msg);
    if (name != NULL) str_name(name->data, name->len);
}

static void err_msg_char_name(const char *msg, const char *name, linepos_t epoint) {
    new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror(msg);
    str_name((const uint8_t *)name, strlen(name));
}

static void err_msg_big_integer(const char *msg, unsigned int bits, Obj *val, linepos_t epoint) {
    char msg2[256];
    sprintf(msg2, msg, bits);
    adderror(msg2);
    err_msg_variable(val, epoint);
}

static int notdefines_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct notdefines_s *a = cavltree_container_of(aa, struct notdefines_s, node);
    const struct notdefines_s *b = cavltree_container_of(bb, struct notdefines_s, node);
    if (a->file_list != b->file_list) return a->file_list > b->file_list ? 1 : -1;
    if (a->epoint.line != b->epoint.line) return a->epoint.line > b->epoint.line ? 1 : -1;
    if (a->epoint.pos != b->epoint.pos) return a->epoint.pos > b->epoint.pos ? 1 : -1;
    return str_cmp(&a->cfname, &b->cfname);
}

static void notdefines_free(struct avltree_node *aa) {
    struct notdefines_s *a = avltree_container_of(aa, struct notdefines_s, node);
    free((uint8_t *)a->cfname.data);
    free(a);
}

static struct notdefines_s *lastnd = NULL;
void err_msg_not_defined2(const str_t *name, Namespace *l, bool down, linepos_t epoint) {
    struct notdefines_s *tmp2;
    struct avltree_node *b;

    if (constcreated && pass < max_pass) return;

    if (lastnd == NULL) {
        lastnd = (struct notdefines_s *)mallocx(sizeof *lastnd);
    }

    if (name->data != NULL) {
        str_cfcpy(&lastnd->cfname, name);
        lastnd->file_list = l->file_list;
        lastnd->epoint = l->epoint;
        lastnd->pass = pass;
        b = avltree_insert(&lastnd->node, &notdefines, notdefines_compare);
        if (b != NULL) {
            tmp2 = avltree_container_of(b, struct notdefines_s, node);
            if (tmp2->pass == pass) {
                return;
            }
            tmp2->pass = pass;
        } else {
            if (lastnd->cfname.data == name->data) str_cpy(&lastnd->cfname, name);
            else str_cfcpy(&lastnd->cfname, NULL);
            lastnd = NULL;
        }
    }

    new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror("not defined");
    if (name->data != NULL) {
        str_name(name->data, name->len);
    } else {
        ssize_t count = (ssize_t)name->len;
        adderror(" '");
        while (count != 0) {
            adderror((count > 0) ? "+" : "-");
            count += (count > 0) ? -1 : 1;
        }
        adderror("'");
    }

    if (l->file_list == NULL) {
        struct linepos_s nopoint = {0, 0};
        new_error_msg(SV_NOTE, current_file_list, &nopoint);
        adderror("searched in the global scope");
    } else {
        new_error_msg(SV_NOTE, l->file_list, &l->epoint);
        if (down) adderror("searched in this scope and in all it's parents");
        else adderror("searched in this object only");
    }
}

static void err_msg_no_addressing(Severity_types severity, atype_t addrtype, linepos_t epoint) {
    new_error_msg(severity, current_file_list, epoint);
    adderror("no");
    if (addrtype == A_NONE) adderror(" implied");
    for (; (addrtype & MAX_ADDRESS_MASK) != 0; addrtype <<= 4) {
        const char *txt = "?";
        switch ((Address_types)((addrtype & 0xf000) >> 12)) {
        case A_NONE: continue;
        case A_IMMEDIATE: txt = " immediate"; break;
        case A_IMMEDIATE_SIGNED: txt = " signed immediate"; break;
        case A_XR: txt = " x indexed"; break;
        case A_YR: txt = " y indexed"; break;
        case A_ZR: txt = " z indexed"; break;
        case A_SR: txt = " stack"; break;
        case A_RR: txt = " data stack"; break;
        case A_DR: txt = " direct page"; break;
        case A_BR: txt = " data bank"; break;
        case A_KR: txt = " program bank"; break;
        case A_I: txt = " indirect"; break;
        case A_LI: txt = " long indirect"; break;
        }
        adderror(txt);
    }
    adderror(" addressing mode for opcode");
}

static void err_msg_no_register(Register *val, linepos_t epoint) {
    new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror("no register '");
    adderror2(val->data, val->len);
    adderror("' addressing mode for opcode");
}

static void err_msg_no_lot_operand(size_t opers, linepos_t epoint) {
    char msg2[256];
    new_error_msg(SV_ERROR, current_file_list, epoint);
    sprintf(msg2, "no %" PRIuSIZE " operand addressing mode for opcode", opers);
    adderror(msg2);
}

static void err_msg_cant_broadcast(const char *msg, size_t v1, size_t v2, linepos_t epoint) {
    char msg2[256];
    new_error_msg(SV_ERROR, current_file_list, epoint);
    sprintf(msg2, msg, v1, v2);
    adderror(msg2);
}

static void err_msg_key_error(Obj *val, const char *msg, linepos_t epoint) {
    new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror(msg);
    err_msg_variable(val, epoint);
}

void err_msg_output(const Error *val) {
    switch (val->num) {
    case ERROR___NOT_DEFINED: err_msg_not_defined2(&val->u.notdef.ident, val->u.notdef.names, val->u.notdef.down, &val->epoint);break;
    case ERROR__INVALID_OPER: err_msg_invalid_oper(val->u.invoper.op, val->u.invoper.v1, val->u.invoper.v2, &val->epoint);break;
    case ERROR____STILL_NONE: err_msg_still_none(NULL, &val->epoint); break;
    case ERROR_____CANT_IVAL:
    case ERROR_____CANT_UVAL:
    case ERROR______NOT_UVAL: new_error_msg(SV_ERROR, current_file_list, &val->epoint); err_msg_big_integer(terr_error[val->num - 0x40], val->u.intconv.bits, val->u.intconv.val, &val->epoint);break;
    case ERROR_ADDRESS_LARGE:
    case ERROR____NO_FORWARD:
    case ERROR_REQUIREMENTS_:
    case ERROR______CONFLICT:
    case ERROR_NOT_TWO_CHARS:
    case ERROR_NUMERIC_OVERF:
    case ERROR_NEGFRAC_POWER:
    case ERROR___EMPTY_RANGE:
    case ERROR__EMPTY_STRING:
    case ERROR__BYTES_NEEDED:
    case ERROR___NO_LAST_GAP:
    case ERROR__NOT_ONE_CHAR:
    case ERROR__NO_BYTE_ADDR:
    case ERROR__NO_WORD_ADDR:
    case ERROR__NO_LONG_ADDR:
    case ERROR_NO_ZERO_VALUE:
    case ERROR_OUT_OF_MEMORY:
    case ERROR__ADDR_COMPLEX:
    case ERROR_DIVISION_BY_Z: err_msg_str_name(terr_error[val->num - 0x40], NULL, &val->epoint);break;
    case ERROR__NOT_KEYVALUE:
    case ERROR__NOT_HASHABLE:
    case ERROR_____CANT_SIGN:
    case ERROR______CANT_ABS:
    case ERROR______CANT_INT:
    case ERROR______CANT_LEN:
    case ERROR_____CANT_SIZE:
    case ERROR_____CANT_BOOL: err_msg_char_name(terr_error[val->num - 0x40], val->u.objname, &val->epoint);break;
    case ERROR_NO_ADDRESSING: err_msg_no_addressing(SV_ERROR, val->u.addressing, &val->epoint);break;
    case ERROR___NO_REGISTER: err_msg_no_register(val->u.reg, &val->epoint);break;
    case ERROR___NO_LOT_OPER: err_msg_no_lot_operand(val->u.opers, &val->epoint);break;
    case ERROR_CANT_BROADCAS: err_msg_cant_broadcast(terr_error[val->num - 0x40], val->u.broadcast.v1, val->u.broadcast.v2, &val->epoint);break;
    case ERROR___MATH_DOMAIN:
    case ERROR_LOG_NON_POSIT:
    case ERROR_SQUARE_ROOT_N:
    case ERROR___INDEX_RANGE:
    case ERROR_____KEY_ERROR: err_msg_key_error(val->u.key, terr_error[val->num - 0x40], &val->epoint);break;
    default: break;
    }
}

void err_msg_output_and_destroy(Error *val) {
    err_msg_output(val);
    val_destroy(&val->v);
}

void err_msg_wrong_type(const Obj *val, Type *expected, linepos_t epoint) {
    new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror("wrong type '");
    adderror(val->obj->name);
    if (expected != NULL) {
        adderror("', expected '");
        adderror(expected->name);
    }
    adderror("'");
}

void err_msg_cant_calculate(const str_t *name, linepos_t epoint) {
    err_msg_str_name("can't calculate stable value", name, epoint);
}

void err_msg_still_none(const str_t *name, linepos_t epoint) {
    if ((constcreated || !fixeddig) && pass < max_pass) return;
    new_error_msg(SV_NONEERROR, current_file_list, epoint);
    adderror("can't calculate this");
    if (name != NULL) str_name(name->data, name->len);
}

void err_msg_not_defined(const str_t *name, linepos_t epoint) {
    err_msg_str_name("not defined", name, epoint);
}

static void err_msg_double_note(struct file_list_s *cflist, linepos_t epoint, const str_t *labelname2) {
    new_error_msg(SV_NOTE, cflist, epoint);
    adderror("original definition of");
    str_name(labelname2->data, labelname2->len);
    adderror(" was here");
}

void err_msg_star_assign(linepos_t epoint) {
    new_error_msg2(diagnostic_errors.star_assign, epoint);
    adderror("label defined instead of variable multiplication for compatibility [-Wstar-assign]");
}

void err_msg_compound_note(linepos_t epoint) {
    static unsigned once;
    if (once != pass) {
        new_error_msg(SV_NOTE, current_file_list, epoint);
        adderror("for reserving space use '.fill x' or '.byte ?' [-Wpitfalls]");
        once = pass;
    }
}

void err_msg_byte_note(linepos_t epoint) {
    static unsigned int once;
    if (once != pass) {
        new_error_msg(SV_NOTE, current_file_list, epoint);
        adderror("for long strings mixed with bytes please use the '.text' directive [-Wpitfalls]");
        once = pass;
    }
}

void err_msg_char_note(const char *directive, linepos_t epoint) {
    new_error_msg(SV_NOTE, current_file_list, epoint);
    adderror("for signed values '");
    adderror(directive);
    adderror("' is a better fit [-Wpitfalls]");
}

void err_msg_immediate_note(linepos_t epoint) {
    new_error_msg(SV_NOTE, current_file_list, epoint);
    adderror("to accept signed values use the '#+' operator [-Wpitfalls]");
}

void err_msg_symbol_case(const str_t *labelname1, Label *l, linepos_t epoint) {
    new_error_msg2(diagnostic_errors.case_symbol, epoint);
    adderror("symbol case mismatch");
    str_name(labelname1->data, labelname1->len);
    adderror(" [-Wcase-symbol]");
    err_msg_double_note(l->file_list, &l->epoint, &l->name);
}

static void err_msg_double_defined2(const char *msg, Severity_types severity, struct file_list_s *cflist, const str_t *labelname2, linepos_t epoint2) {
    new_error_msg(severity, cflist, epoint2);
    adderror(msg);
    str_name(labelname2->data, labelname2->len);
}

void err_msg_not_variable(Label *l, const str_t *labelname2, linepos_t epoint2) {
    err_msg_double_defined2("not a variable", SV_ERROR, current_file_list, labelname2, epoint2);
    err_msg_double_note(l->file_list, &l->epoint, labelname2);
}

void err_msg_double_defined(Label *l, const str_t *labelname2, linepos_t epoint2) {
    err_msg_double_defined2("duplicate definition", SV_ERROR, current_file_list, labelname2, epoint2);
    err_msg_double_note(l->file_list, &l->epoint, labelname2);
}

void err_msg_double_definedo(struct file_list_s *cflist, linepos_t epoint, const str_t *labelname2, linepos_t epoint2) {
    err_msg_double_defined2("duplicate definition", SV_ERROR, current_file_list, labelname2, epoint2);
    err_msg_double_note(cflist, epoint, labelname2);
}

void err_msg_shadow_defined(Label *l, Label *l2) {
    err_msg_double_defined2("shadow definition", diagnostic_errors.shadow ? SV_ERROR : SV_WARNING, l2->file_list, &l2->name, &l2->epoint);
    adderror(" [-Wshadow]");
    err_msg_double_note(l->file_list, &l->epoint, &l2->name);
}

void err_msg_shadow_defined2(Label *l) {
    err_msg_double_defined2("shadow definition of built-in", diagnostic_errors.shadow ? SV_ERROR : SV_WARNING, l->file_list, &l->name, &l->epoint);
    adderror(" [-Wshadow]");
}

static void err_msg_unused(const char *msg, bool error, Label *l) {
    new_error_msg(error ? SV_ERROR : SV_WARNING, l->file_list, &l->epoint);
    adderror(msg);
    str_name(l->name.data, l->name.len);
}

void err_msg_unused_macro(Label *l) {
    err_msg_unused("unused macro", diagnostic_errors.unused.macro, l);
    adderror(" [-Wunused-macro]");
}

void err_msg_unused_label(Label *l) {
    err_msg_unused("unused label", diagnostic_errors.unused.label, l);
    adderror(" [-Wunused-label]");
}

void err_msg_unused_const(Label *l) {
    err_msg_unused("unused const", diagnostic_errors.unused.consts, l);
    adderror(" [-Wunused-const]");
}

void err_msg_unused_variable(Label *l) {
    err_msg_unused("unused variable", diagnostic_errors.unused.variable, l);
    adderror(" [-Wunused-variable]");
}

static void err_msg_invalid_oper2(const Oper *op, Obj *v1, Obj *v2, linepos_t epoint) {
    adderror(op->name);
    adderror("' of ");
    adderror(v1->obj->name);
    err_msg_variable(v1, epoint);
    if (v2 != NULL) {
        adderror(" and ");
        adderror(v2->obj->name);
        err_msg_variable(v2, epoint);
    }
    adderror(" not possible");
}

void err_msg_invalid_oper(const Oper *op, Obj *v1, Obj *v2, linepos_t epoint) {
    if (v1->obj == ERROR_OBJ) {
        err_msg_output((const Error *)v1);
        return;
    }
    if (v2 != NULL && v2->obj == ERROR_OBJ) {
        err_msg_output((const Error *)v2);
        return;
    }

    new_error_msg(SV_ERROR, current_file_list, epoint);
    err_msg_invalid_oper2(op, v1, v2, epoint);
}

void err_msg_argnum(size_t num, size_t min, size_t max, linepos_t epoint) {
    size_t n;
    char line[1024];

    new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror("expected ");
    n = min;
    if (min == max) adderror("exactly ");
    else if (num < min) adderror("at least ");
    else {n = max; adderror("at most "); }
    switch (n) {
    case 0: adderror("no arguments"); break;
    case 1: adderror("one argument"); break;
    default: sprintf(line, "%" PRIuSIZE " arguments", n); adderror(line); break;
    }
    if (num != 0) {
        sprintf(line, ", got %" PRIuSIZE, num);
        adderror(line);
    }
}

void err_msg_bool(Error_types no, Obj *o, linepos_t epoint) {
    const char *name = o->obj->name;
    new_error_msg2(diagnostic_errors.strict_bool, epoint);
    adderror(terr_error[no - 0x40]);
    str_name((const uint8_t *)name, strlen(name));
    adderror(" [-Wstrict-bool]");
}

void err_msg_bool_val(Error_types no, unsigned int bits, Obj *o, linepos_t epoint) {
    new_error_msg2(diagnostic_errors.strict_bool, epoint);
    err_msg_big_integer(terr_error[no - 0x40], bits, o, epoint);
    adderror(" [-Wstrict-bool]");
}

void err_msg_bool_oper(oper_t op) {
    new_error_msg2(diagnostic_errors.strict_bool, op->epoint3);
    err_msg_invalid_oper2(op->op, op->v1, op->v2, op->epoint3);
    adderror(" [-Wstrict-bool]");
}

void err_msg_implied_reg(linepos_t epoint) {
    err_msg_no_addressing(diagnostic_errors.implied_reg ? SV_ERROR : SV_WARNING, A_NONE, epoint);
    adderror(" [-Wimplied-reg]");
}

void err_msg_jmp_bug(linepos_t epoint) {
    new_error_msg2(diagnostic_errors.jmp_bug, epoint);
    adderror( "possible jmp ($xxff) bug [-Wjmp-bug]");
}

void err_msg_pc_wrap(linepos_t epoint) {
    if (!diagnostics.pc_wrap) return;
    new_error_msg2(diagnostic_errors.pc_wrap, epoint);
    adderror("processor program counter overflow [-Wpc-wrap]");
}

void err_msg_mem_wrap(linepos_t epoint) {
    if (!diagnostics.mem_wrap) return;
    new_error_msg2(diagnostic_errors.mem_wrap, epoint);
    adderror("compile offset overflow [-Wmem-wrap]");
}

void err_msg_label_left(linepos_t epoint) {
    new_error_msg2(diagnostic_errors.label_left, epoint);
    adderror("label not on left side [-Wlabel-left]");
}

void err_msg_branch_page(int by, linepos_t epoint) {
    char msg2[256];
    new_error_msg2(diagnostic_errors.branch_page, epoint);
    sprintf(msg2, "branch crosses page by %+d bytes [-Wbranch-page]", by);
    adderror(msg2);
}

void err_msg_alias(uint32_t a, uint32_t b, linepos_t epoint) {
    char name[4];
    new_error_msg2(diagnostic_errors.alias, epoint);
    adderror("instruction '");
    name[0] = (char)(a >> 16);
    name[1] = (char)(a >> 8);
    name[2] = (char)a;
    name[3] = '\0';
    adderror(name);
    adderror("' is alias of '");
    name[0] = (char)(b >> 16);
    name[1] = (char)(b >> 8);
    name[2] = (char)b;
    adderror(name);
    adderror("' [-Walias]");
}

void err_msg_unknown_char(uchar_t ch, const str_t *name, linepos_t epoint) {
    uint8_t line[256], *s = line;
    new_error_msg(SV_ERROR, current_file_list, epoint);
    adderror("can't encode character '");
    if (ch != 0 && ch < 0x80) *s++ = (uint8_t)ch; else s = utf8out(ch, s);
    sprintf((char *)s, "' ($%02" PRIx32 ") in encoding '", ch); adderror((char *)line);
    adderror2(name->data, name->len);
    adderror("'");
}

static const uint8_t *printline(const struct file_list_s *cfile, linepos_t epoint, const uint8_t *line, FILE *f) {
    const struct file_s *file;
    if (epoint->line == 0) return NULL;
    file = cfile->file;
    if (line == NULL) line = &file->data[file->line[epoint->line - 1]];
    fprintf(f, ":%" PRIuline ":%" PRIlinepos, epoint->line, ((file->encoding == E_UTF8) ? (linecpos_t)calcpos(line, epoint->pos) : epoint->pos) + 1);
    return line;
}

static inline void print_error(FILE *f, const struct errorentry_s *err) {
    const struct file_list_s *cflist = err->file_list;
    linepos_t epoint = &err->epoint;
    const uint8_t *line = NULL;
    bool bold;

    if (cflist != &file_list) {
        if (cflist != included_from) {
            included_from = cflist;
            while (included_from->parent != &file_list) {
                fputs((included_from == cflist) ? "In file included from " : "                      ", f);
                if (print_use_color) fputs("\33[01m", f);
                printable_print((const uint8_t *)included_from->parent->file->realname, f);
                printline(included_from->parent, &included_from->epoint, NULL, f);
                included_from = included_from->parent;
                if (print_use_color) fputs("\33[m\33[K", f);
                fputs((included_from->parent != &file_list) ? ",\n" : ":\n", f);
            }
            included_from = cflist;
        }
        if (print_use_color) fputs("\33[01m", f);
        printable_print((const uint8_t *)cflist->file->realname, f);
        line = printline(cflist, epoint, (err->line_len != 0) ? (const uint8_t *)(err + 1) : NULL, f);
        fputs(": ", f);
    } else {
        if (print_use_color) fputs("\33[01m", f);
        printable_print((const uint8_t *)prgname, f);
        fputs(": ", f);
    }
    switch (err->severity) {
    case SV_NOTE: if (print_use_color) fputs("\33[30m", f); fputs("note: ", f); bold = false; break;
    case SV_WARNING: if (print_use_color) fputs("\33[35m", f); fputs("warning: ", f); bold = true; break;
    case SV_NONEERROR:
    case SV_ERROR: if (print_use_color) fputs("\33[31m", f); fputs("error: ", f); bold = true; break;
    case SV_FATAL: if (print_use_color) fputs("\33[31m", f); fputs("fatal error: ", f); bold = true; break;
    default: bold = false;
    }
    if (print_use_color) fputs(bold ? "\33[m\33[01m" : "\33[m\33[K", f);
#ifdef COLOR_OUTPUT
    print_use_bold = print_use_color && bold;
#endif
    printable_print2(((const uint8_t *)(err + 1)) + err->line_len, f, err->error_len);
    if (print_use_color && bold) fputs("\33[m\33[K", f);
#ifdef COLOR_OUTPUT
    print_use_bold = false;
#endif
    putc('\n', f);
    if (arguments.caret && line != NULL) {
        putc(' ', f);
        printable_print(line, f);
        fputs("\n ", f);
        caret_print(line, f, epoint->pos);
        fputs(print_use_color ? "\33[01;32m^\33[m\33[K\n" : "^\n", f);
    }
}

#ifdef COLOR_OUTPUT
static void color_detect(FILE *f) {
    char const *term = getenv ("TERM");
    print_use_color = term != NULL && strcmp(term, "dumb") != 0 && isatty(fileno(f)) == 1;
}
#else
#define color_detect(f) {}
#endif

bool error_print() {
    const struct errorentry_s *err, *err2, *err3;
    size_t pos;
    bool noneerr = false, anyerr = false, usenote;
    FILE *ferr;
    struct linepos_s nopoint = {0, 0};

    if (arguments.error != NULL) {
        ferr = dash_name(arguments.error) ? stdout : file_open(arguments.error, "wt");
        if (ferr == NULL) {
            err_msg_file(ERROR_CANT_WRTE_ERR, arguments.error, &nopoint);
            ferr = stderr;
        }
    } else ferr = stderr;

    color_detect(ferr);

    warnings = errors = 0;
    close_error();

    for (pos = 0; pos < error_list.len; pos = ALIGN(pos + (sizeof *err) + err->line_len + err->error_len)) {
        err = (const struct errorentry_s *)&error_list.data[pos];
        switch (err->severity) {
        case SV_NONEERROR: anyerr = true; break;
        case SV_NOTE:
        case SV_WARNING:
            break;
        default: noneerr = true; anyerr = true; break;
        }
    }

    err2 = err3 = NULL;
    usenote = false;
    for (pos = 0; pos < error_list.len; pos = ALIGN(pos + (sizeof *err) + err->line_len + err->error_len)) {
        err = (const struct errorentry_s *)&error_list.data[pos];
        switch (err->severity) {
        case SV_NOTE:
            if (!usenote) continue;
            if (err3 != NULL) {
                if (err->severity != err3->severity || err->file_list != err3->file_list ||
                        err->line_len != err3->line_len || err->error_len != err3->error_len ||
                        err->epoint.line != err3->epoint.line || err->epoint.pos != err3->epoint.pos ||
                        memcmp(err + 1, err3 + 1, err->line_len + err->error_len) != 0) {
                    print_error(ferr, err3);
                }
            }
            err3 = err2;
            err2 = err;
            usenote = false;
            continue;
        case SV_WARNING: 
            if (!arguments.warning) {
                usenote = false;
                continue; 
            }
            warnings++; 
            if (anyerr) {
                usenote = false;
                continue; 
            }
            break;
        case SV_NONEERROR: 
            if (noneerr) {
                usenote = false;
                continue; 
            }
            /* fall through */
        default: 
            errors++; 
            break;
        }
        if (err3 != NULL) print_error(ferr, err3);
        err3 = err2;
        err2 = err;
        usenote = true;
    }
    if (err3 != NULL) print_error(ferr, err3);
    if (err2 != NULL) print_error(ferr, err2);
    color_detect(stderr);
    if (ferr != stderr && ferr != stdout) fclose(ferr); else fflush(ferr);
    return errors != 0;
}

void error_reset(void) {
    error_list.len = error_list.header_pos = 0;
    current_file_list = &file_list;
    included_from = &file_list;
}

void err_init(const char *name) {
    prgname = name;
    setvbuf(stderr, NULL, _IOLBF, 1024);
    color_detect(stderr);
    avltree_init(&file_list.members);
    error_list.len = error_list.max = error_list.header_pos = 0;
    error_list.data = NULL;
    current_file_list = &file_list;
    included_from = &file_list;
    avltree_init(&notdefines);
}

void err_destroy(void) {
    avltree_destroy(&file_list.members, file_list_free);
    free(lastfl);
    free(error_list.data);
    avltree_destroy(&notdefines, notdefines_free);
    free(lastnd);
}

void fatal_error(const char *txt) {
    if (txt != NULL) {
        if (print_use_color) fputs("\33[01m", stderr);
        printable_print((const uint8_t *)((prgname != NULL) ? prgname : "64tass"), stderr);
        fputs(print_use_color ? ": \33[31m" : ": ", stderr);
        fputs("fatal error: ", stderr);
        if (print_use_color) fputs("\33[m\33[01m", stderr);
        fputs(txt, stderr);
        return;
    }
    if (print_use_color) fputs("\33[m\33[K", stderr);
    putc('\n', stderr);
}

void err_msg_out_of_memory2(void)
{
    fatal_error("out of memory");
    fatal_error(NULL);
    exit(EXIT_FAILURE);
}

void err_msg_out_of_memory(void)
{
    error_print();
    err_msg_out_of_memory2();
}

void err_msg_file(Error_types no, const char *prm, linepos_t epoint) {
    mbstate_t ps;
    const char *s;
    wchar_t w;
    uint8_t s2[10];
    size_t n, i = 0;
    ssize_t l;
    int err = errno;

#ifdef _WIN32
    setlocale(LC_ALL, "");
#endif
    s = strerror(err);
    n = strlen(s);

    new_error_msg(SV_FATAL, current_file_list, epoint);
    adderror(terr_fatal[no - 0xc0]);
    adderror(" '");
    adderror(prm);
    adderror("': ");
    memset(&ps, 0, sizeof ps);
    while (i < n) {
        if (s[i] != 0 && (s[i] & 0x80) == 0) {
            adderror2((const uint8_t *)s + i, 1);
            i++;
            continue;
        }
        l = (ssize_t)mbrtowc(&w, s + i, n - i,  &ps);
        if (l < 1) {
            w = (uint8_t)s[i];
            if (w == 0 || l == 0) break;
            l = 1;
        }
        s2[utf8out((uchar_t)w, s2) - s2] = 0;
        adderror((char *)s2);
        i += (size_t)l;
    }
#ifdef _WIN32
    setlocale(LC_ALL, "C");
#endif
}

void error_status(void) {
    printf("Error messages:    ");
    if (errors != 0) printf("%u\n", errors); else puts("None");
    printf("Warning messages:  ");
    if (warnings != 0) printf("%u\n", warnings); else puts("None");
}

linecpos_t interstring_position(linepos_t epoint, const uint8_t *data, size_t i) {
    if (epoint->line == lpoint.line && strlen((const char *)pline) > epoint->pos) {
        uint8_t q = pline[epoint->pos];
        if (q == '"' || q == '\'') {
            linecpos_t pos = epoint->pos + 1;
            size_t pp = 0;
            while (pp < i && pline[pos] != 0) {
                unsigned int ln2;
                if (pline[pos] == q) {
                    if (pline[pos + 1] != q) break;
                    pos++;
                }
                ln2 = utf8len(pline[pos]);
                if (memcmp(pline + pos, data + pp, ln2) != 0) break;
                pos += ln2; pp += ln2;
            }
            if (pp == i) {
                return pos;
            }
        }
    }
    return epoint->pos;
}

bool error_serious(void) {
    const struct errorentry_s *err;
    size_t pos;
    close_error();
    for (pos = 0; pos < error_list.len; pos = ALIGN(pos + (sizeof *err) + err->line_len + err->error_len)) {
        err = (const struct errorentry_s *)&error_list.data[pos];
        switch (err->severity) {
        case SV_NOTE:
        case SV_WARNING: break;
        default: return true;
        }
    }
    return false;
}
