/*
    $Id: error.h 1500 2017-04-30 01:38:58Z soci $

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
#ifndef ERROR_H
#define ERROR_H
#include "attributes.h"
#include "stdbool.h"
#include "errors_e.h"
#include "avl.h"
#include "obj.h"
#include "str.h"

struct file_s;

struct file_list_s {
    struct linepos_s epoint;
    struct file_s *file;
    struct avltree_node node;
    struct file_list_s *parent;
    struct avltree members;
};

#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE || _POSIX_VERSION || _POSIX2_VERSION
#define COLOR_OUTPUT
extern bool print_use_color;
extern bool print_use_bold;
#else
#define print_use_color false
#define print_use_bold false
#endif

struct Label;
struct Oper;
struct Error;

extern void err_msg(Error_types, const void *);
extern void err_msg2(Error_types, const void *, linepos_t);
extern void err_msg_wrong_type(const Obj *, struct Type *, linepos_t);
extern void err_msg_cant_calculate(const str_t *, linepos_t);
extern void err_msg_still_none(const str_t *, linepos_t);
extern void err_msg_invalid_oper(const struct Oper *, Obj *, Obj *, linepos_t);
extern void err_msg_double_definedo(struct file_list_s *, linepos_t, const str_t *, linepos_t);
extern void err_msg_not_variable(struct Label *, const str_t *, linepos_t);
extern void err_msg_double_defined(struct Label *, const str_t *, linepos_t);
extern void err_msg_shadow_defined(struct Label *, struct Label *);
extern void err_msg_shadow_defined2(struct Label *);
extern void err_msg_unused_macro(struct Label *);
extern void err_msg_unused_label(struct Label *);
extern void err_msg_unused_const(struct Label *);
extern void err_msg_unused_variable(struct Label *);
extern void err_msg_not_defined(const str_t *, linepos_t);
extern void err_msg_not_defined2(const str_t *, struct Namespace *, bool, linepos_t);
extern void err_msg_symbol_case(const str_t *, struct Label *, linepos_t);
extern void err_msg_file(Error_types, const char *, linepos_t);
extern void err_msg_output(const struct Error *);
extern void err_msg_output_and_destroy(struct Error *);
extern void err_msg_argnum(size_t, size_t, size_t, linepos_t);
extern void err_msg_bool(Error_types, Obj *, linepos_t);
extern void err_msg_bool_oper(struct oper_s *);
extern void err_msg_bool_val(Error_types, unsigned int, Obj *, linepos_t);
extern void err_msg_implied_reg(linepos_t);
extern void err_msg_jmp_bug(linepos_t);
extern void err_msg_pc_wrap(linepos_t);
extern void err_msg_mem_wrap(linepos_t);
extern void err_msg_label_left(linepos_t);
extern void err_msg_branch_page(int, linepos_t);
extern void err_msg_alias(uint32_t, uint32_t, linepos_t);
extern void err_msg_deprecated(Error_types, linepos_t);
extern void err_msg_unknown_char(uchar_t, const str_t *, linepos_t);
extern void err_msg_star_assign(linepos_t);
extern void err_msg_compound_note(linepos_t);
extern void err_msg_byte_note(linepos_t);
extern void err_msg_char_note(const char *, linepos_t);
extern void err_msg_immediate_note(linepos_t);
extern void error_reset(void);
extern bool error_print(void);
extern struct file_list_s *enterfile(struct file_s *, linepos_t);
extern void exitfile(void);
extern void err_init(const char *);
extern void err_destroy(void);
extern void fatal_error(const char *);
extern void NO_RETURN err_msg_out_of_memory2(void);
extern void NO_RETURN err_msg_out_of_memory(void);
extern void error_status(void);
extern bool error_serious(void);
extern linecpos_t interstring_position(linepos_t, const uint8_t *, size_t);

static inline MALLOC void *mallocx(size_t l) {
    void *m = malloc(l);
    if (m == NULL) err_msg_out_of_memory();
    return m;
}

static inline MUST_CHECK void *reallocx(void *o, size_t l) {
    void *m = realloc(o, l);
    if (m == NULL) err_msg_out_of_memory();
    return m;
}

#endif
