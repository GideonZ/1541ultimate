/*
    $Id: typeobj.c 1494 2017-04-28 11:01:15Z soci $

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
#include "typeobj.h"
#include <string.h>
#include "variables.h"
#include "eval.h"
#include "error.h"

#include "strobj.h"
#include "operobj.h"
#include "intobj.h"
#include "boolobj.h"
#include "listobj.h"
#include "noneobj.h"
#include "errorobj.h"

static Type obj;

Type *TYPE_OBJ = &obj;

static struct Slot *values_free[32];

#define ALIGN sizeof(int *)

void new_type(Type *t, Type_types type, const char *name, size_t length) {
    t->v.obj = TYPE_OBJ;
    t->v.refcount = 1;
    t->type = type;
    t->length = (length + (ALIGN - 1)) / ALIGN;
    t->slot = &values_free[t->length];
    t->name = name;
    obj_init(t);
}

static MUST_CHECK Obj *create(Obj *v1, linepos_t UNUSED(epoint)) {
    switch (v1->obj->type) {
    case T_NONE:
    case T_ERROR: 
    case T_TYPE: return val_reference(v1);
    default: break;
    }
    return val_reference(&v1->obj->v);
}

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    return o1 == o2;
}

static MUST_CHECK Error *hash(Obj *o1, int *hs, linepos_t UNUSED(epoint)) {
    Type *v1 = (Type *)o1;
    *hs = v1->type;
    return NULL;
}

static MUST_CHECK Obj *repr(Obj *o1, linepos_t epoint, size_t maxsize) {
    Type *v1 = (Type *)o1;
    Str *v;
    uint8_t *s;
    const char *name;
    size_t ln, ln2;
    if (epoint == NULL) return NULL;
    name = v1->name;
    ln = strlen(name);
    ln2 = ln + 9;
    if (ln2 > maxsize) return NULL;
    v = new_str(ln2);
    v->chars = ln2;
    s = v->data;
    memcpy(s, "<type '", 7);
    s += 7;
    memcpy(s, name, ln);
    s[ln] = '\'';
    s[ln + 1] = '>';
    return &v->v;
}

static inline int icmp(const Type *vv1, const Type *vv2) {
    Type_types v1 = vv1->type;
    Type_types v2 = vv2->type;
    if (v1 < v2) return -1;
    return (v1 > v2) ? 1 : 0;
}

static MUST_CHECK Obj *calc2(oper_t op) {
    Type *v1 = (Type *)op->v1;
    Obj *o2 = op->v2;
    size_t args;

    switch (o2->obj->type) {
    case T_TYPE:
        {
            Type *v2 = (Type *)o2;
            int val = icmp(v1, v2);
            switch (op->op->op) {
            case O_CMP:
                if (val < 0) return (Obj *)ref_int(minus1_value);
                return (Obj *)ref_int(int_value[(val > 0) ? 1 : 0]);
            case O_EQ: return truth_reference(val == 0);
            case O_NE: return truth_reference(val != 0);
            case O_MIN:
            case O_LT: return truth_reference(val < 0);
            case O_LE: return truth_reference(val <= 0);
            case O_MAX:
            case O_GT: return truth_reference(val > 0);
            case O_GE: return truth_reference(val >= 0);
            default: break;
            }
        }
        break;
    case T_FUNCARGS:
        if (op->op == &o_FUNC) {
            args = ((Funcargs *)o2)->len;
            if (args != 1) {
                err_msg_argnum(args, 1, 1, op->epoint2);
                return (Obj *)ref_none();
            }
            o2 = ((Funcargs *)o2)->val[0].val;
            switch (o2->obj->type) {
            case T_LIST:
            case T_TUPLE: 
                if (v1 != LIST_OBJ && v1 != TUPLE_OBJ && v1 != TYPE_OBJ) {
                    List *v2 = (List *)o2;
                    Obj **vals;
                    size_t i;
                    bool error = true;
                    List *v = (List *)val_alloc(o2->obj);
                    v->data = vals = list_create_elements(v, v2->len);
                    for (i = 0;i < v2->len; i++) {
                        Obj *val = v1->create(v2->data[i], op->epoint2);
                        if (val->obj == ERROR_OBJ) { if (error) {err_msg_output((Error *)val); error = false;} val_destroy(val); val = (Obj *)ref_none(); }
                        vals[i] = val;
                    }
                    v->len = i;
                    return (Obj *)v;
                }
                /* fall through */
            default: return v1->create(o2, op->epoint2);
            }
        }
        break;
    case T_TUPLE:
    case T_LIST:
    case T_DICT:
        if (op->op != &o_MEMBER && op->op != &o_X) {
            return o2->obj->rcalc2(op);
        }
        break;
    case T_NONE:
    case T_ERROR:
        return val_reference(o2);
    default: break;
    }
    return obj_oper_error(op);
}


void typeobj_init(void) {
    new_type(&obj, T_TYPE, "type", sizeof(Type));
    obj.create = create;
    obj.same = same;
    obj.hash = hash;
    obj.repr = repr;
    obj.calc2 = calc2;
}

void typeobj_names(void) {
    new_builtin("type", val_reference(&TYPE_OBJ->v));
}
