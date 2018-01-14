/*
    $Id: registerobj.h 1362 2017-03-19 19:45:45Z soci $

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
#ifndef REGISTEROBJ_H
#define REGISTEROBJ_H
#include "obj.h"
#include "stdbool.h"

extern struct Type *REGISTER_OBJ;

typedef struct Register {
    Obj v;
    size_t len;
    size_t chars;
    uint8_t *data;
    uint8_t val[4];
} Register;

extern void registerobj_init(void);
extern void registerobj_names(void);
extern bool registerobj_createnames(uint32_t);

static inline Register *ref_register(Register *v1) {
    v1->v.refcount++; return v1;
}
#endif
