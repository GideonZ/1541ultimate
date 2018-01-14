/*
    $Id: listobj.h 1093 2016-05-12 10:50:04Z soci $

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
#ifndef LISTOBJ_H
#define LISTOBJ_H
#include "obj.h"
#include "values.h"

extern struct Type *LIST_OBJ;
extern struct Type *TUPLE_OBJ;
extern struct Type *ADDRLIST_OBJ;
extern struct Type *COLONLIST_OBJ;

typedef struct List {
    Obj v;
    size_t len;
    Obj **data;
    Obj *val[5];
} List;
typedef struct List Tuple;
typedef struct List Addrlist;
typedef struct List Colonlist;

extern Tuple *null_tuple;
extern List *null_list;
extern Addrlist *null_addrlist;

extern void listobj_init(void);
extern void listobj_names(void);
extern void listobj_destroy(void);

static inline List *ref_list(List *v1) {
    v1->v.refcount++; return v1;
}

static inline Tuple *ref_tuple(Tuple *v1) {
    v1->v.refcount++; return v1;
}

static inline Addrlist *ref_addrlist(Addrlist *v1) {
    v1->v.refcount++; return v1;
}

static inline Colonlist *ref_colonlist(Colonlist *v1) {
    v1->v.refcount++; return v1;
}

static inline MUST_CHECK List *new_list(void) {
    return (List *)val_alloc(LIST_OBJ);
}
static inline MUST_CHECK Tuple *new_tuple(void) {
    return (Tuple *)val_alloc(TUPLE_OBJ);
}
static inline MUST_CHECK Addrlist *new_addrlist(void) {
    return (Addrlist *)val_alloc(ADDRLIST_OBJ);
}
static inline MUST_CHECK Colonlist *new_colonlist(void) {
    return (Colonlist *)val_alloc(COLONLIST_OBJ);
}

extern Obj **list_create_elements(List *, size_t);
#endif
