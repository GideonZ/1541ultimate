/*
    $Id: errorobj.h 1506 2017-04-30 13:52:04Z soci $

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
#ifndef ERROROBJ_H
#define ERROROBJ_H
#include "obj.h"
#include "errors_e.h"
#include "str.h"
#include "stdbool.h"

extern struct Type *ERROR_OBJ;

struct Namespace;
struct Oper;
struct Register;

typedef struct Error {
    Obj v;
    Error_types num;
    struct linepos_s epoint;
    union {
        struct {
            struct Oper *op;
            Obj *v1;
            Obj *v2;
        } invoper;
        struct {
            str_t ident;
            struct Namespace *names;
            bool down;
        } notdef;
        struct {
            unsigned int bits;
            Obj *val;
        } intconv;
        const char *objname;
        uint32_t addressing;
        struct Register *reg;
        size_t opers;
        struct {
            size_t v1;
            size_t v2;
        } broadcast;
        Obj *key;
    } u;
} Error;

extern void errorobj_init(void);

extern MALLOC Error *new_error(Error_types, linepos_t);
extern MALLOC Error *new_error_mem(linepos_t);
extern MALLOC Error *new_error_key(Error_types, Obj *, linepos_t);

#endif
