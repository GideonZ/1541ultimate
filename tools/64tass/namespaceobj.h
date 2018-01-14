/*
    $Id: namespaceobj.h 1407 2017-03-30 15:10:21Z soci $

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
#ifndef NAMESPACEOBJ_H
#define NAMESPACEOBJ_H
#include "obj.h"
#include "avl.h"

extern struct Type *NAMESPACE_OBJ;

typedef struct Namespace {
    Obj v;
    size_t len;
    struct avltree members;
    const struct file_list_s *file_list;
    struct linepos_s epoint;
} Namespace;

extern void namespaceobj_init(void);

struct Label;

struct namespacekey_s {
    int hash;
    struct Label *key;
    struct avltree_node node;
};

static inline Namespace *ref_namespace(Namespace *v1) {
    v1->v.refcount++; return v1;
}

extern MALLOC Namespace *new_namespace(const struct file_list_s *, linepos_t);
extern MUST_CHECK Obj *namespace_member(struct oper_s *, Namespace *);

extern void namespacekey_free(struct namespacekey_s *);
extern struct namespacekey_s *namespacekey_alloc(void);
extern void destroy_namespacekeys(void);
#endif
