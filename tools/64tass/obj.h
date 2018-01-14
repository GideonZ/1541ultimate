/*
    $Id: obj.h 1407 2017-03-30 15:10:21Z soci $

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
#ifndef OBJ_H
#define OBJ_H
#include "attributes.h"
#include "inttypes.h"

struct oper_s;
struct Namespace;
struct Type;

typedef struct Obj {
    struct Type *obj;
    size_t refcount;
} Obj;

typedef struct Lbl_s {
    Obj v;
    line_t sline;
    size_t waitforp;
    const struct file_list_s *file_list;
    struct Namespace *parent;
} Lbl;

typedef struct Iter {
    Obj v;
    void *iter;
    size_t val;
    Obj *data;
} Iter;

typedef struct Funcargs {
    Obj v;
    size_t len;
    struct values_s *val;
} Funcargs;

typedef struct Default {
    Obj v;
    int *dummy;
} Default;

static inline Obj *val_reference(Obj *v1) {
    v1->refcount++; return v1;
}

extern MUST_CHECK Obj *obj_oper_error(struct oper_s *);
extern void obj_init(struct Type *);
extern void objects_init(void);
extern void objects_destroy(void);

extern struct Type *LBL_OBJ;
extern struct Type *DEFAULT_OBJ;
extern struct Type *ITER_OBJ;
extern struct Type *FUNCARGS_OBJ;
extern Default *default_value;

static inline Default *ref_default(void) {
    default_value->v.refcount++; return default_value;
}
#endif
