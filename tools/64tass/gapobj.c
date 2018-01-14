/*
    $Id: gapobj.c 1494 2017-04-28 11:01:15Z soci $

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
#include "gapobj.h"
#include <string.h>
#include "error.h"
#include "eval.h"
#include "variables.h"
#include "values.h"

#include "strobj.h"
#include "operobj.h"
#include "intobj.h"
#include "boolobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "errorobj.h"

static Type obj;

Type *GAP_OBJ = &obj;
Gap *gap_value;

static MUST_CHECK Obj *create(Obj *v1, linepos_t epoint) {
    switch (v1->obj->type) {
    case T_NONE:
    case T_ERROR:
    case T_GAP: return val_reference(v1);
    default: break;
    }
    err_msg_wrong_type(v1, NULL, epoint);
    return (Obj *)ref_none();
}

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    return o1 == o2;
}

static MUST_CHECK Error *hash(Obj *UNUSED(v1), int *hs, linepos_t UNUSED(epoint)) {
    *hs = 0; /* whatever, there's only one */
    return NULL;
}

static MUST_CHECK Obj *repr(Obj *UNUSED(v1), linepos_t UNUSED(epoint), size_t maxsize) {
    Str *v;
    if (1 > maxsize) return NULL;
    v = new_str(1);
    v->chars = 1;
    v->data[0] = '?';
    return &v->v;
}

static MUST_CHECK Obj *function(Obj *v1, Func_types UNUSED(f), linepos_t UNUSED(epoint)) {
    return val_reference(v1);
}

static MUST_CHECK Obj *calc1(oper_t op) {
    switch (op->op->op) {
    case O_BANK: 
    case O_HIGHER:
    case O_LOWER:
    case O_HWORD:
    case O_WORD:
    case O_BSWORD:
    case O_INV:
    case O_NEG:
    case O_POS:
        return val_reference(op->v1);
    default: break;
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *calc2(oper_t op) {
    Obj *v2 = op->v2;
    switch (v2->obj->type) {
    case T_GAP:
        switch (op->op->op) {
        case O_CMP: return (Obj *)ref_int(int_value[0]);
        case O_GE: 
        case O_LE:
        case O_EQ: return (Obj *)ref_bool(true_value);
        case O_NE:
        case O_MIN:
        case O_LT:
        case O_MAX:
        case O_GT: return (Obj *)ref_bool(false_value);
        case O_ADD:
        case O_SUB:
        case O_MUL:
        case O_DIV:
        case O_MOD:
        case O_EXP:
        case O_AND:
        case O_OR:
        case O_XOR:
        case O_LSHIFT:
        case O_RSHIFT: return val_reference(op->v1);
        default: break;
        }
        break;
    case T_STR:
    case T_BOOL:
    case T_INT:
    case T_BITS:
    case T_FLOAT:
    case T_CODE:
    case T_ADDRESS:
    case T_BYTES:
    case T_REGISTER:
        switch (op->op->op) {
        case O_EQ: return (Obj *)ref_bool(false_value);
        case O_NE: return (Obj *)ref_bool(true_value);
        case O_ADD:
        case O_SUB:
        case O_MUL:
        case O_DIV:
        case O_MOD:
        case O_EXP:
        case O_AND:
        case O_OR:
        case O_XOR:
        case O_LSHIFT:
        case O_RSHIFT: return val_reference(op->v1);
        default: break;
        }
        break;
    case T_TUPLE:
    case T_LIST:
    case T_DICT:
        if (op->op != &o_MEMBER && op->op != &o_X) {
            return v2->obj->rcalc2(op);
        }
        break;
    case T_NONE:
    case T_ERROR:
        return val_reference(v2);
    default: break;
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *rcalc2(oper_t op) {
    Obj *v1 = op->v1;
    switch (v1->obj->type) {
    case T_GAP: return calc2(op);
    case T_STR:
    case T_BOOL:
    case T_INT:
    case T_BITS:
    case T_FLOAT:
    case T_CODE:
    case T_ADDRESS:
    case T_BYTES:
    case T_REGISTER:
        switch (op->op->op) {
        case O_EQ: return (Obj *)ref_bool(false_value);
        case O_NE: return (Obj *)ref_bool(true_value);
        case O_ADD:
        case O_SUB:
        case O_MUL:
        case O_DIV:
        case O_MOD:
        case O_EXP:
        case O_AND:
        case O_OR:
        case O_XOR: return val_reference(op->v2);
        default: break;
        }
        break;
    case T_NONE:
    case T_ERROR:
    case T_TUPLE:
    case T_LIST:
        if (op->op != &o_IN) {
            return v1->obj->calc2(op);
        }
        break;
    default: break;
    }
    return obj_oper_error(op);
}

void gapobj_init(void) {
    new_type(&obj, T_GAP, "gap", sizeof(Gap));
    obj.create = create;
    obj.same = same;
    obj.hash = hash;
    obj.repr = repr;
    obj.function = function;
    obj.calc1 = calc1;
    obj.calc2 = calc2;
    obj.rcalc2 = rcalc2;

    gap_value = (Gap *)val_alloc(GAP_OBJ);
}

void gapobj_names(void) {
    new_builtin("gap", val_reference(&GAP_OBJ->v));
}

void gapobj_destroy(void) {
#ifdef DEBUG
    if (gap_value->v.refcount != 1) fprintf(stderr, "gap %" PRIuSIZE "\n", gap_value->v.refcount - 1);
#endif

    val_destroy(&gap_value->v);
}
