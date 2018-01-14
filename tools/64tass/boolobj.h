/*
    $Id: boolobj.h 1506 2017-04-30 13:52:04Z soci $

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
#ifndef BOOLOBJ_H
#define BOOLOBJ_H
#include "obj.h"
#include "stdbool.h"

extern struct Type *BOOL_OBJ;

typedef struct Bool {
    Obj v;
    const char *name;
} Bool;

extern Bool *true_value;
extern Bool *false_value;
extern Bool *bool_value[2];

extern void boolobj_init(void);
extern void boolobj_names(void);
extern void boolobj_destroy(void);

static inline Bool *ref_bool(Bool *v1) {
    v1->v.refcount++; return v1;
}

static inline MUST_CHECK Obj *truth_reference(bool i) {
    return (Obj *)ref_bool(bool_value[i ? 1 : 0]);
}

struct Int;
struct Float;

extern MUST_CHECK struct Obj *int_from_bool(const struct Bool *);
extern MUST_CHECK struct Float *float_from_bool(const struct Bool *);
#endif
