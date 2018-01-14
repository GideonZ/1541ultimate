/*
    $Id: dictobj.h 1358 2017-03-19 11:12:45Z soci $

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
#ifndef DICTOBJ_H
#define DICTOBJ_H
#include "obj.h"
#include "avl.h"

extern struct Type *DICT_OBJ;

typedef struct Dict {
    Obj v;
    size_t len;
    struct avltree members;
    Obj *def;
} Dict;

extern void dictobj_init(void);
extern void dictobj_names(void);

struct pair_s {
    int hash;
    Obj *key;
    Obj *data;
    struct avltree_node node;
};

extern Obj *dictobj_parse(struct values_s *, unsigned int);

#endif
