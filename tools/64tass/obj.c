/*
    $Id: obj.c 1494 2017-04-28 11:01:15Z soci $

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
#include "obj.h"
#include <string.h>
#include "eval.h"
#include "error.h"
#include "values.h"
#include "macro.h"

#include "boolobj.h"
#include "floatobj.h"
#include "strobj.h"
#include "macroobj.h"
#include "intobj.h"
#include "listobj.h"
#include "namespaceobj.h"
#include "addressobj.h"
#include "codeobj.h"
#include "registerobj.h"
#include "bytesobj.h"
#include "bitsobj.h"
#include "functionobj.h"
#include "dictobj.h"
#include "operobj.h"
#include "gapobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "labelobj.h"
#include "errorobj.h"
#include "mfuncobj.h"
#include "identobj.h"

static Type lbl_obj;
static Type default_obj;
static Type iter_obj;
static Type funcargs_obj;

Type *LBL_OBJ = &lbl_obj;
Type *DEFAULT_OBJ = &default_obj;
Type *ITER_OBJ = &iter_obj;
Type *FUNCARGS_OBJ = &funcargs_obj;
Default *default_value;

MUST_CHECK Obj *obj_oper_error(oper_t op) {
    Obj *v1, *v2;
    Error *err;
    switch (op->op->op) {
    case O_EQ: return truth_reference(op->v1 == op->v2 || op->v1->obj->same(op->v1, op->v2));
    case O_NE: return truth_reference(op->v1 != op->v2 && !op->v1->obj->same(op->v1, op->v2));
    case O_FUNC:
    case O_INDEX: v2 = NULL; break;
    default: v2 = op->v2; break;
    }
    v1 = op->v1;
    err = new_error(ERROR__INVALID_OPER, op->epoint3);
    err->u.invoper.op = op->op;
    err->u.invoper.v1 = (v1 != NULL) ? ((v1->refcount != 0) ? val_reference(v1) : v1) : NULL;
    err->u.invoper.v2 = (v2 != NULL) ? ((v2->refcount != 0) ? val_reference(v2) : v2) : NULL;
    return &err->v;
}

static MUST_CHECK Obj *invalid_create(Obj *v1, linepos_t epoint) {
    switch (v1->obj->type) {
    case T_NONE:
    case T_ERROR: return val_reference(v1);
    default: break;
    }
    err_msg_wrong_type(v1, NULL, epoint);
    return (Obj *)ref_none();
}

static FAST_CALL bool invalid_same(const Obj *v1, const Obj *v2) {
    return v1->obj == v2->obj;
}

static MUST_CHECK Error *generic_invalid(Obj *v1, linepos_t epoint, Error_types num) {
    Error *err;
    if (v1->obj == ERROR_OBJ) {
        return (Error *)val_reference(v1);
    }
    err = new_error(num, epoint);
    err->u.objname = v1->obj->name;
    return err;
}

static MUST_CHECK Obj *invalid_truth(Obj *v1, Truth_types UNUSED(type), linepos_t epoint) {
    return (Obj *)generic_invalid(v1, epoint, ERROR_____CANT_BOOL);
}

static MUST_CHECK Error *invalid_hash(Obj *v1, int *UNUSED(hash), linepos_t epoint) {
    return generic_invalid(v1, epoint, ERROR__NOT_HASHABLE);
}

static MUST_CHECK Obj *invalid_repr(Obj *v1, linepos_t epoint, size_t maxsize) {
    Str *v;
    uint8_t *s;
    const char *name;
    size_t len, len2;
    if (epoint == NULL) return NULL;
    if (v1->obj == ERROR_OBJ) {
        return val_reference(v1);
    }
    name = v1->obj->name;
    len2 = strlen(name);
    len = len2 + 2;
    if (len > maxsize) return NULL;
    v = new_str(len);
    v->chars = len;
    s = v->data;
    *s++ = '<';
    memcpy(s, name, len2);
    s[len2] = '>';
    return &v->v;
}

static MUST_CHECK Obj *invalid_calc1(oper_t op) {
    return obj_oper_error(op);
}

static MUST_CHECK Obj *invalid_calc2(oper_t op) {
    Obj *o2 = op->v2;
    if (o2 == &none_value->v || o2->obj == ERROR_OBJ) {
        return val_reference(o2);
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *invalid_rcalc2(oper_t op) {
    Obj *o1 = op->v1;
    if (o1 == &none_value->v || o1->obj == ERROR_OBJ) {
        return val_reference(o1);
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *invalid_slice(Obj *UNUSED(v1), oper_t op, size_t indx) {
    if (op->v2->obj == ERROR_OBJ) {
        return val_reference(op->v2);
    }
    if (indx != 0) {
        Obj *o2 = op->v2;
        Funcargs *args = (Funcargs *)o2;
        err_msg_argnum(args->len, 1, indx, op->epoint2);
        return (Obj *)ref_none();
    }
    return obj_oper_error(op);
}

static MUST_CHECK Error *invalid_ival(Obj *v1, ival_t *UNUSED(iv), unsigned int UNUSED(bits), linepos_t epoint) {
    return generic_invalid(v1, epoint, ERROR______CANT_INT);
}

static MUST_CHECK Error *invalid_uval(Obj *v1, uval_t *UNUSED(uv), unsigned int UNUSED(bits), linepos_t epoint) {
    return generic_invalid(v1, epoint, ERROR______CANT_INT);
}

static MUST_CHECK Error *invalid_uval2(Obj *v1, uval_t *uv, unsigned int bits, linepos_t epoint) {
    return v1->obj->uval(v1, uv, bits, epoint);
}

static FAST_CALL Obj *invalid_address(Obj *v1, uint32_t *am) {
    *am = A_NONE;
    return v1;
}

static MUST_CHECK Obj *invalid_sign(Obj *v1, linepos_t epoint) {
    return (Obj *)generic_invalid(v1, epoint, ERROR_____CANT_SIGN);
}

static MUST_CHECK Obj *invalid_function(Obj *v1, Func_types f, linepos_t epoint) {
    return (Obj *)generic_invalid(v1, epoint, (f == TF_ABS) ? ERROR______CANT_ABS : ERROR______CANT_INT);
}

static MUST_CHECK Obj *invalid_len(Obj *v1, linepos_t epoint) {
    return (Obj *)generic_invalid(v1, epoint, ERROR______CANT_LEN);
}

static MUST_CHECK Obj *invalid_size(Obj *v1, linepos_t epoint) {
    return (Obj *)generic_invalid(v1, epoint, ERROR_____CANT_SIZE);
}

static MUST_CHECK Iter *invalid_getiter(Obj *v1) {
    Iter *v = (Iter *)val_alloc(ITER_OBJ);
    v->data = val_reference(v1);
    v->iter = NULL;
    v->val = 1;
    return v;
}

static MUST_CHECK Obj *invalid_next(Iter *v1) {
    if (v1->val == 0) return NULL;
    v1->val = 0;
    return val_reference(v1->data);
}

static FAST_CALL void iter_destroy(Obj *o1) {
    Iter *v1 = (Iter *)o1;
    if (v1->iter != &v1->val) free(v1->iter);
    val_destroy(v1->data);
}

static FAST_CALL void iter_garbage(Obj *o1, int i) {
    Iter *v1 = (Iter *)o1;
    Obj *v;
    switch (i) {
    case -1:
        v1->data->refcount--;
        return;
    case 0:
        if (v1->iter != &v1->val) free(v1->iter);
        return;
    case 1:
        v = v1->data;
        if ((v->refcount & SIZE_MSB) != 0) {
            v->refcount -= SIZE_MSB - 1;
            v->obj->garbage(v, 1);
        } else v->refcount++;
        return;
    }
}

static MUST_CHECK Obj *iter_next(Iter *v1) {
    if (v1->iter == NULL) return invalid_next(v1);
    return v1->data->obj->next(v1);
}

static FAST_CALL bool lbl_same(const Obj *o1, const Obj *o2) {
    const Lbl *v1 = (const Lbl *)o1, *v2 = (const Lbl *)o2;
    return o2->obj == LBL_OBJ && v1->sline == v2->sline && v1->waitforp == v2->waitforp && v1->file_list == v2->file_list && v1->parent == v2->parent;
}

static FAST_CALL bool default_same(const Obj *o1, const Obj *o2) {
    return o1 == o2;
}

static FAST_CALL bool funcargs_same(const Obj *o1, const Obj *o2) {
    const Funcargs *v1 = (const Funcargs *)o1, *v2 = (const Funcargs *)o2;
    return o2->obj == FUNCARGS_OBJ && v1->val == v2->val && v1->len == v2->len;
}

void obj_init(Type *obj) {
    obj->create = invalid_create;
    obj->destroy = NULL;
    obj->garbage = NULL;
    obj->same = invalid_same;
    obj->truth = invalid_truth;
    obj->hash = invalid_hash;
    obj->repr = invalid_repr;
    obj->calc1 = invalid_calc1;
    obj->calc2 = invalid_calc2;
    obj->rcalc2 = invalid_rcalc2;
    obj->slice = invalid_slice;
    obj->ival = invalid_ival;
    obj->uval = invalid_uval;
    obj->uval2 = invalid_uval2;
    obj->address = invalid_address;
    obj->sign = invalid_sign;
    obj->function = invalid_function;
    obj->len = invalid_len;
    obj->size = invalid_size;
    obj->getiter = invalid_getiter;
    obj->next = invalid_next;
}

void objects_init(void) {
    boolobj_init();
    floatobj_init();
    addressobj_init();
    codeobj_init();
    strobj_init();
    registerobj_init();
    listobj_init();
    bytesobj_init();
    bitsobj_init();
    intobj_init();
    functionobj_init();
    dictobj_init();
    labelobj_init();
    namespaceobj_init();
    macroobj_init();
    errorobj_init();
    operobj_init();
    gapobj_init();
    typeobj_init();
    noneobj_init();
    mfuncobj_init();
    identobj_init();

    new_type(&lbl_obj, T_LBL, "lbl", sizeof(Lbl));
    lbl_obj.same = lbl_same;
    new_type(&default_obj, T_DEFAULT, "default", sizeof(Default));
    default_obj.same = default_same;
    new_type(&iter_obj, T_ITER, "iter", sizeof(Iter));
    iter_obj.destroy = iter_destroy;
    iter_obj.garbage = iter_garbage;
    iter_obj.next = iter_next;
    new_type(&funcargs_obj, T_FUNCARGS, "funcargs", sizeof(Funcargs));
    funcargs_obj.same = funcargs_same;

    default_value = (Default *)val_alloc(DEFAULT_OBJ);
}

void objects_destroy(void) {
    listobj_destroy();
    bitsobj_destroy();
    bytesobj_destroy();
    strobj_destroy();
    boolobj_destroy();
    intobj_destroy();
    gapobj_destroy();
    noneobj_destroy();

#ifdef DEBUG
    if (default_value->v.refcount != 1) fprintf(stderr, "default %" PRIuSIZE "\n", default_value->v.refcount - 1);
#endif

    val_destroy(&default_value->v);
}
