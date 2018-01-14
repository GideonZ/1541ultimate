/*
    $Id: typeobj.h 1483 2017-04-17 14:01:47Z soci $

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
#ifndef TYPEOBJ_H
#define TYPEOBJ_H
#include "obj.h"
#include "stdbool.h"

extern struct Type *TYPE_OBJ;

typedef enum Truth_types {
    TRUTH_BOOL, TRUTH_ALL, TRUTH_ANY
} Truth_types;

typedef enum Type_types {
    T_NONE, T_BOOL, T_BITS, T_INT, T_FLOAT, T_BYTES, T_STR, T_GAP, T_ADDRESS,
    T_IDENT, T_ANONIDENT, T_ERROR, T_OPER, T_COLONLIST, T_TUPLE, T_LIST,
    T_DICT, T_MACRO, T_SEGMENT, T_UNION, T_STRUCT, T_MFUNC, T_CODE, T_LBL,
    T_DEFAULT, T_ITER, T_REGISTER, T_FUNCTION, T_ADDRLIST, T_FUNCARGS, T_TYPE,
    T_LABEL, T_NAMESPACE
} Type_types;

typedef enum Func_types {
    TF_ABS, TF_TRUNC, TF_CEIL, TF_FLOOR, TF_ROUND
} Func_types;

struct Error;

typedef MUST_CHECK Obj *(*iter_next_t)(Iter *);

typedef struct Type {
    Obj v;
    Type_types type;
    const char *name;
    struct Slot **slot;
    size_t length;
    Obj *(*create)(Obj *, linepos_t) MUST_CHECK;
    void (*destroy)(Obj *) FAST_CALL;
    void (*garbage)(Obj *, int) FAST_CALL;
    bool (*same)(const Obj *, const Obj *) FAST_CALL;
    Obj *(*truth)(Obj *, Truth_types, linepos_t) MUST_CHECK;
    struct Error *(*hash)(Obj *, int *, linepos_t) MUST_CHECK;
    Obj *(*repr)(Obj *, linepos_t, size_t) MUST_CHECK;
    Obj *(*calc1)(struct oper_s *) MUST_CHECK;
    Obj *(*calc2)(struct oper_s *) MUST_CHECK;
    Obj *(*rcalc2)(struct oper_s *) MUST_CHECK;
    Obj *(*slice)(Obj *, struct oper_s *, size_t) MUST_CHECK;
    struct Error *(*ival)(Obj *, ival_t *, unsigned int, linepos_t) MUST_CHECK;
    struct Error *(*uval)(Obj *, uval_t *, unsigned int, linepos_t) MUST_CHECK;
    struct Error *(*uval2)(Obj *, uval_t *, unsigned int, linepos_t) MUST_CHECK;
    Obj *(*address)(Obj *, uint32_t *) FAST_CALL;
    Obj *(*sign)(Obj *, linepos_t) MUST_CHECK;
    Obj *(*function)(Obj *, Func_types, linepos_t) MUST_CHECK;
    Obj *(*len)(Obj *, linepos_t) MUST_CHECK;
    Obj *(*size)(Obj *, linepos_t) MUST_CHECK;
    Iter *(*getiter)(Obj *) MUST_CHECK;
    iter_next_t next;
} Type;

extern void typeobj_init(void);
extern void typeobj_names(void);
extern void new_type(Type *, Type_types, const char *, size_t);

#endif
