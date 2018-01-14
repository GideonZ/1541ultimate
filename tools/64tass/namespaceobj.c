/*
    $Id: namespaceobj.c 1494 2017-04-28 11:01:15Z soci $

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
#include "namespaceobj.h"
#include <string.h>
#include "variables.h"
#include "eval.h"
#include "error.h"
#include "arguments.h"
#include "64tass.h"

#include "intobj.h"
#include "listobj.h"
#include "strobj.h"
#include "operobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "labelobj.h"
#include "errorobj.h"
#include "identobj.h"

static Type obj;

Type *NAMESPACE_OBJ = &obj;

static void namespace_free(struct avltree_node *aa)
{
    struct namespacekey_s *a = avltree_container_of(aa, struct namespacekey_s, node);
    val_destroy(&a->key->v);
    namespacekey_free(a);
}

static void namespace_free2(struct avltree_node *aa)
{
    struct namespacekey_s *a = avltree_container_of(aa, struct namespacekey_s, node);
    namespacekey_free(a);
}

static void garbage1(struct avltree_node *aa)
{
    struct namespacekey_s *a = avltree_container_of(aa, struct namespacekey_s, node);
    a->key->v.refcount--;
}

static void garbage2(struct avltree_node *aa)
{
    struct namespacekey_s *a = avltree_container_of(aa, struct namespacekey_s, node);
    Obj *v = &a->key->v;
    if ((v->refcount & SIZE_MSB) != 0) {
        v->refcount -= SIZE_MSB - 1;
        v->obj->garbage(v, 1);
    } else v->refcount++;
}

static FAST_CALL void destroy(Obj *o1) {
    Namespace *v1 = (Namespace *)o1;
    avltree_destroy(&v1->members, namespace_free);
}

static FAST_CALL void garbage(Obj *o1, int i) {
    Namespace *v1 = (Namespace *)o1;
    switch (i) {
    case -1:
        avltree_destroy(&v1->members, garbage1);
        return;
    case 0:
        avltree_destroy(&v1->members, namespace_free2);
        return;
    case 1:
        avltree_destroy(&v1->members, garbage2);
        return;
    }
}

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    const Namespace *v1 = (const Namespace *)o1, *v2 = (const Namespace *)o2;
    const struct avltree_node *n;
    const struct avltree_node *n2;
    if (o2->obj != NAMESPACE_OBJ) return false;
    n = avltree_first(&v1->members);
    n2 = avltree_first(&v2->members);
    while (n != NULL && n2 != NULL) {
        const struct namespacekey_s *p = cavltree_container_of(n, struct namespacekey_s, node);
        const struct namespacekey_s *p2 = cavltree_container_of(n2, struct namespacekey_s, node);
        if ((p->key == NULL) != (p2->key == NULL)) return false;
        if (p->key != NULL && p2->key != NULL && !p->key->v.obj->same((Obj *)p->key, (Obj *)p2->key)) return false;
        n = avltree_next(n);
        n2 = avltree_next(n2);
    }
    return n == n2;
}

static MUST_CHECK Obj *repr(Obj *o1, linepos_t epoint, size_t maxsize) {
    const Namespace *v1 = (const Namespace *)o1;
    size_t i = 0, j, ln = 2, chars = 2;
    Obj **vals;
    Str *str;
    Tuple *tuple = NULL;
    uint8_t *s;

    if (v1->len != 0) {
        ln = v1->len;
        chars = ln + 1;
        if (chars < 1) err_msg_out_of_memory(); /* overflow */
        if (chars > maxsize) return NULL;
        tuple = new_tuple();
        tuple->data = vals = list_create_elements(tuple, ln);
        ln = chars;
        if (v1->len != 0) {
            const struct avltree_node *n;
            for (n = avltree_first(&v1->members); n != NULL; n = avltree_next(n)) {
                const struct namespacekey_s *p = cavltree_container_of(n, struct namespacekey_s, node);
                Obj *key = (Obj *)p->key;
                Obj *v = key->obj->repr(key, epoint, maxsize - chars);
                if (v == NULL || v->obj != STR_OBJ) {
                    tuple->len = i;
                    val_destroy(&tuple->v);
                    return v;
                }
                str = (Str *)v;
                ln += str->len;
                if (ln < str->len) err_msg_out_of_memory(); /* overflow */
                chars += str->chars;
                if (chars > maxsize) {
                    tuple->len = i;
                    val_destroy(&tuple->v);
                    val_destroy(v);
                    return NULL;
                }
                vals[i++] = v;
            }
        }
        tuple->len = i;
    }
    str = new_str(ln);
    str->chars = chars;
    s = str->data;
    *s++ = '{';
    for (j = 0; j < i; j++) {
        Str *str2 = (Str *)vals[j];
        if (j != 0) *s++ = ',';
        if (str2->len != 0) {
            memcpy(s, str2->data, str2->len);
            s += str2->len;
        }
    }
    *s = '}';
    if (tuple != NULL) val_destroy(&tuple->v);
    return &str->v;
}

MUST_CHECK Obj *namespace_member(oper_t op, Namespace *v1) {
    Obj *o2 = op->v2;
    Error *err;
    Label *l;
    switch (o2->obj->type) {
    case T_IDENT:
        {
            Ident *v2 = (Ident *)o2;
            l = find_label2(&v2->name, v1);
            if (l != NULL) {
                if (diagnostics.case_symbol && str_cmp(&v2->name, &l->name) != 0) err_msg_symbol_case(&v2->name, l, &v2->epoint);
                touch_label(l);
                return val_reference(l->value);
            }
            if (!referenceit) {
                return (Obj *)ref_none();
            }
            err = new_error(ERROR___NOT_DEFINED, &v2->epoint);
            err->u.notdef.names = ref_namespace(v1);
            err->u.notdef.ident = v2->name;
            err->u.notdef.down = false;
            return &err->v;
        }
    case T_ANONIDENT:
        {
            Anonident *v2 = (Anonident *)o2;
            ssize_t count;
            l = find_anonlabel2(v2->count, v1);
            if (l != NULL) {
                touch_label(l);
                return val_reference(l->value);
            }
            if (!referenceit) {
                return (Obj *)ref_none();
            }
            count = v2->count;
            if (count >= 0) count++;
            err = new_error(ERROR___NOT_DEFINED, &v2->epoint);
            err->u.notdef.names = ref_namespace(v1);
            err->u.notdef.ident.len = (size_t)count;
            err->u.notdef.ident.data = NULL;
            err->u.notdef.down = false;
            return &err->v;
        }
    case T_TUPLE:
    case T_LIST: return o2->obj->rcalc2(op);
    case T_ERROR:
    case T_NONE: return val_reference(o2);
    default: return obj_oper_error(op);
    }
}

MALLOC Namespace *new_namespace(const struct file_list_s *file_list, linepos_t epoint) {
    Namespace *val = (Namespace *)val_alloc(NAMESPACE_OBJ);
    avltree_init(&val->members);
    val->file_list = file_list;
    val->epoint = *epoint;
    val->len = 0;
    return val;
}

void namespaceobj_init(void) {
    new_type(&obj, T_NAMESPACE, "namespace", sizeof(Namespace));
    obj.destroy = destroy;
    obj.garbage = garbage;
    obj.same = same;
    obj.repr = repr;
}

#define SLOTS 128

static void *namespacekeys;
#ifndef DEBUG
static void *namespacekey_next;
#endif

void namespacekey_free(struct namespacekey_s *val) {
#ifdef DEBUG
    free(val);
#else
    *((void **)val) = namespacekey_next;
    namespacekey_next = (void *)val;
#endif
}

struct namespacekey_s *namespacekey_alloc(void) {
#ifdef DEBUG
    struct namespacekey_s *val = (struct namespacekey_s *)mallocx(sizeof *val);
#else
    struct namespacekey_s *val = (struct namespacekey_s *)namespacekey_next;
    if (val == NULL) {
        size_t i;
        uint8_t *n = (uint8_t *)mallocx(sizeof(void *) + sizeof(struct namespacekey_s) * SLOTS);
        *((void **)n) = namespacekeys;
        namespacekeys = (void *)n;
        n += sizeof(void *);
        val = (struct namespacekey_s *)n;
        for (i = 0; i < (SLOTS - 1); i++, n += sizeof(struct namespacekey_s)) {
            *((void **)n) = (void *)(n + sizeof(struct namespacekey_s));
        }
        *((void **)n) = NULL;
    }
    namespacekey_next = *(void **)val;
#endif
    return val;
}

void destroy_namespacekeys(void) {
    void *n = namespacekeys, *n2;
    while (n != NULL) {
        n2 = *((void **)n);
        free(n);
        n = n2;
    }
}

