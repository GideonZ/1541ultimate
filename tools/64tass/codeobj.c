/*
    $Id: codeobj.c 1498 2017-04-29 17:49:46Z soci $

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
#include "codeobj.h"
#include <string.h>
#include "eval.h"
#include "mem.h"
#include "64tass.h"
#include "section.h"
#include "variables.h"
#include "error.h"
#include "values.h"
#include "arguments.h"

#include "boolobj.h"
#include "floatobj.h"
#include "namespaceobj.h"
#include "listobj.h"
#include "intobj.h"
#include "bitsobj.h"
#include "bytesobj.h"
#include "operobj.h"
#include "gapobj.h"
#include "typeobj.h"
#include "noneobj.h"
#include "errorobj.h"

static Type obj;

Type *CODE_OBJ = &obj;

static MUST_CHECK Obj *create(Obj *v1, linepos_t epoint) {
    switch (v1->obj->type) {
    case T_NONE:
    case T_ERROR:
    case T_CODE: return val_reference(v1);
    default: break;
    }
    err_msg_wrong_type(v1, NULL, epoint);
    return (Obj *)ref_none();
}

static FAST_CALL void destroy(Obj *o1) {
    Code *v1 = (Code *)o1;
    val_destroy(v1->addr);
    val_destroy(&v1->names->v);
}

static FAST_CALL void garbage(Obj *o1, int i) {
    Code *v1 = (Code *)o1;
    Obj *v;
    switch (i) {
    case -1:
        v1->addr->refcount--;
        v1->names->v.refcount--;
        return;
    case 0:
        return;
    case 1:
        v = v1->addr;
        if ((v->refcount & SIZE_MSB) != 0) {
            v->refcount -= SIZE_MSB - 1;
            v->obj->garbage(v, 1);
        } else v->refcount++;
        v = &v1->names->v;
        if ((v->refcount & SIZE_MSB) != 0) {
            v->refcount -= SIZE_MSB - 1;
            v->obj->garbage(v, 1);
        } else v->refcount++;
        return;
    }
}

static MUST_CHECK Error *access_check(const Code *v1, linepos_t epoint) {
    if ((v1->requires & ~current_section->provides) != 0) {
        return new_error(ERROR_REQUIREMENTS_, epoint);
    }
    if ((v1->conflicts & current_section->provides) != 0) {
        return new_error(ERROR______CONFLICT, epoint);
    }
    return NULL;
}

static FAST_CALL bool same(const Obj *o1, const Obj *o2) {
    const Code *v1 = (const Code *)o1, *v2 = (const Code *)o2;
    return o2->obj == CODE_OBJ && (v1->addr == v2->addr || v1->addr->obj->same(v1->addr, v2->addr))
        && v1->size == v2->size && v1->offs == v2->offs && v1->dtype == v2->dtype
        && v1->requires == v2->requires && v1->conflicts == v2->conflicts
        && (v1->names == v2->names || v1->names->v.obj->same(&v1->names->v, &v2->names->v));
}

static MUST_CHECK Obj *truth(Obj *o1, Truth_types type, linepos_t epoint) {
    Code *v1 = (Code *)o1;
    Obj *v;
    Error *err;
    err = access_check(v1, epoint);
    if (err != NULL) return &err->v;
    v = v1->addr;
    return v->obj->truth(v, type, epoint);
}

static MUST_CHECK Obj *repr(Obj *o1, linepos_t epoint, size_t maxsize) {
    Code *v1 = (Code *)o1;
    Obj *v;
    if (epoint != NULL) {
        Error *err = access_check(v1, epoint);
        if (err != NULL) return &err->v;
    }
    v = v1->addr;
    return v->obj->repr(v, epoint, maxsize);
}

static MUST_CHECK Error *ival(Obj *o1, ival_t *iv, unsigned int bits, linepos_t epoint) {
    Code *v1 = (Code *)o1;
    Obj *v;
    Error *err = access_check(v1, epoint);
    if (err != NULL) return err;
    v = v1->addr;
    return v->obj->ival(v, iv, bits, epoint);
}

static MUST_CHECK Error *uval(Obj *o1, uval_t *uv, unsigned int bits, linepos_t epoint) {
    Code *v1 = (Code *)o1;
    Obj *v;
    Error *err = access_check(v1, epoint);
    if (err != NULL) return err;
    v = v1->addr;
    return v->obj->uval(v, uv, bits, epoint);
}

static MUST_CHECK Error *uval2(Obj *o1, uval_t *uv, unsigned int bits, linepos_t epoint) {
    Code *v1 = (Code *)o1;
    Obj *v;
    Error *err = access_check(v1, epoint);
    if (err != NULL) return err;
    v = v1->addr;
    return v->obj->uval2(v, uv, bits, epoint);
}

static FAST_CALL Obj *address(Obj *o1, uint32_t *am) {
    Code *v1 = (Code *)o1;
    Obj *v = v1->addr;
    v = v->obj->address(v, am);
    if ((v1->requires & ~current_section->provides) != 0) {
        return o1;
    }
    if ((v1->conflicts & current_section->provides) != 0) {
        return o1;
    }
    return v;
}

MUST_CHECK Obj *float_from_code(Code *v1, linepos_t epoint) {
    Error *err = access_check(v1, epoint);
    if (err != NULL) return &err->v;
    return FLOAT_OBJ->create(v1->addr, epoint);
}

static MUST_CHECK Obj *sign(Obj *o1, linepos_t epoint) {
    Code *v1 = (Code *)o1;
    Obj *v;
    Error *err = access_check(v1, epoint);
    if (err != NULL) return &err->v;
    v = v1->addr;
    return v->obj->sign(v, epoint);
}

static MUST_CHECK Obj *function(Obj *o1, Func_types f, linepos_t epoint) {
    Code *v1 = (Code *)o1;
    Obj *v;
    Error *err = access_check(v1, epoint);
    if (err != NULL) return &err->v;
    v = v1->addr;
    return v->obj->function(v, f, epoint);
}

MUST_CHECK Obj *int_from_code(Code *v1, linepos_t epoint) {
    Error *err = access_check(v1, epoint);
    if (err != NULL) return &err->v;
    return INT_OBJ->create(v1->addr, epoint);
}

static address_t calc_size(const Code *v1) {
    if (v1->offs >= 0) {
        if (v1->size < (uval_t)v1->offs) return 0;
        return v1->size - (uval_t)v1->offs;
    }
    if (v1->size + (uval_t)-v1->offs < v1->size) err_msg_out_of_memory(); /* overflow */
    return v1->size + (uval_t)-v1->offs;
}

static MUST_CHECK Obj *len(Obj *o1, linepos_t UNUSED(epoint)) {
    address_t ln, s;
    Code *v1 = (Code *)o1;
    if (v1->pass == 0) {
        return (Obj *)ref_none();
    }
    ln = (v1->dtype < 0) ? (address_t)-v1->dtype : (address_t)v1->dtype;
    s = calc_size(v1);
    return (Obj *)int_from_size((ln != 0) ? (s / ln) : s);
}

static MUST_CHECK Obj *size(Obj *o1, linepos_t UNUSED(epoint)) {
    Code *v1 = (Code *)o1;
    if (v1->pass == 0) {
        return (Obj *)ref_none();
    }
    return (Obj *)int_from_size(calc_size(v1));
}

MUST_CHECK Obj *bits_from_code(Code *v1, linepos_t epoint) {
    Error *err = access_check(v1, epoint);
    if (err != NULL) return &err->v;
    return BITS_OBJ->create(v1->addr, epoint);
}

MUST_CHECK Obj *bytes_from_code(Code *v1, linepos_t epoint) {
    Error *err = access_check(v1, epoint);
    if (err != NULL) return &err->v;
    return BYTES_OBJ->create(v1->addr, epoint);
}

static MUST_CHECK Obj *code_item(const Code *v1, ssize_t offs2, size_t ln2) {
    int r;
    size_t i2, offs;
    uval_t val;
    if (offs2 < 0) return (Obj *)ref_gap();
    offs = (size_t)offs2 * ln2;
    r = -1;
    for (val = i2 = 0; i2 < ln2; i2++, offs++) {
        r = read_mem(v1->mem, v1->memp, v1->membp, offs);
        if (r < 0) return (Obj *)ref_gap();
        val |= (uval_t)r << (i2 * 8);
    }
    if (v1->dtype < 0 && (r & 0x80) != 0) {
        for (; i2 < sizeof val; i2++) val |= (uval_t)0xff << (i2 * 8);
    }
    return (v1->dtype < 0) ? (Obj *)int_from_ival((ival_t)val) : (Obj *)int_from_uval(val);
}

MUST_CHECK Obj *tuple_from_code(const Code *v1, Type *typ, linepos_t epoint) {
    address_t ln, ln2;
    size_t  i;
    ssize_t offs;
    List *v;
    Obj **vals;

    if (v1->pass != pass) {
        return (Obj *)new_error(ERROR____NO_FORWARD, epoint);
    }

    ln2 = (v1->dtype < 0) ? (address_t)-v1->dtype : (address_t)v1->dtype;
    if (ln2 == 0) ln2 = 1;
    ln = calc_size(v1) / ln2;

    if (ln == 0) {
        return val_reference(typ == TUPLE_OBJ ? &null_tuple->v : &null_list->v);
    }

    v = (List *)val_alloc(typ);
    v->len = ln;
    v->data = vals = list_create_elements(v, ln);
    if (v1->offs >= 0) {
        offs = (ssize_t)(((uval_t)v1->offs + ln2 - 1) / ln2);
    } else {
        offs = -(ssize_t)(((uval_t)-v1->offs + ln2 - 1) / ln2);
    }
    for (i = 0; i < ln; i++, offs++) {
        vals[i] = code_item(v1, offs, ln2);
    }
    return &v->v;
}

static MUST_CHECK Obj *slice(Obj *o1, oper_t op, size_t indx) {
    Obj **vals;
    size_t i;
    address_t ln, ln2;
    size_t offs1;
    ssize_t offs0;
    Code *v1 = (Code *)o1;
    Obj *o2 = op->v2;
    Error *err;
    Funcargs *args = (Funcargs *)o2;
    linepos_t epoint2;

    if (args->len < 1 || args->len > indx + 1) {
        err_msg_argnum(args->len, 1, indx + 1, op->epoint2);
        return (Obj *)ref_none();
    }
    o2 = args->val[indx].val;
    epoint2 = &args->val[indx].epoint;

    if (v1->pass != pass) {
        return (Obj *)new_error(ERROR____NO_FORWARD, op->epoint);
    }

    ln2 = (v1->dtype < 0) ? (address_t)-v1->dtype : (address_t)v1->dtype;
    if (ln2 == 0) ln2 = 1;
    ln = calc_size(v1) / ln2;

    if (v1->offs >= 0) {
        offs0 = (ssize_t)(((uval_t)v1->offs + ln2 - 1) / ln2);
    } else {
        offs0 = -(ssize_t)(((uval_t)-v1->offs + ln2 - 1) / ln2);
    }
    if (o2->obj == LIST_OBJ) {
        List *list = (List *)o2;
        size_t len1 = list->len;
        Tuple *v;
        bool error;

        if (len1 == 0) {
            return (Obj *)ref_tuple(null_tuple);
        }
        v = new_tuple();
        v->data = vals = list_create_elements(v, len1);
        error = true;
        for (i = 0; i < len1; i++) {
            err = indexoffs(list->data[i], ln, &offs1, epoint2);
            if (err != NULL) {
                if (error) {err_msg_output(err); error = false;} 
                val_destroy(&err->v);
                vals[i] = (Obj *)ref_none();
                continue;
            }
            vals[i] = code_item(v1, (ssize_t)offs1 + offs0, ln2);
        }
        v->len = i;
        return &v->v;
    }
    if (o2->obj == COLONLIST_OBJ) {
        uval_t length;
        ival_t offs, end, step;
        Tuple *v;

        err = (Error *)sliceparams((Colonlist *)o2, ln, &length, &offs, &end, &step, epoint2);
        if (err != NULL) return &err->v;

        if (length == 0) {
            return (Obj *)ref_tuple(null_tuple);
        }
        if (v1->pass != pass) {
            return (Obj *)new_error(ERROR____NO_FORWARD, op->epoint);
        }
        v = new_tuple();
        vals = list_create_elements(v, length);
        i = 0;
        while ((end > offs && step > 0) || (end < offs && step < 0)) {
            vals[i++] = code_item(v1, offs + offs0, ln2);
            offs += step;
        }
        v->len = length;
        v->data = vals;
        return &v->v;
    }
    err = indexoffs(o2, ln, &offs1, epoint2);
    if (err != NULL) return &err->v;

    return code_item(v1, (ssize_t)offs1 + offs0, ln2);
}

static MUST_CHECK Obj *calc1(oper_t op) {
    switch (op->op->op) {
    case O_LNOT:
        if (diagnostics.strict_bool) err_msg_bool_oper(op);
        /* fall through */
    case O_BANK:
    case O_HIGHER:
    case O_LOWER:
    case O_HWORD:
    case O_WORD:
    case O_BSWORD:
    case O_STRING:
    case O_INV:
    case O_NEG:
    case O_POS:
        op->v1 = ((Code *)op->v1)->addr;
        return op->v1->obj->calc1(op);
    default: break;
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *calc2(oper_t op) {
    Code *v1 = (Code *)op->v1, *v;
    Obj *o2 = op->v2;
    Error *err;
    if (op->op == &o_MEMBER) {
        return namespace_member(op, v1->names);
    }
    if (op->op == &o_X) {
        if (o2 == &none_value->v || o2->obj == ERROR_OBJ) return val_reference(o2);
        return obj_oper_error(op);
    }
    if (op->op == &o_LAND || op->op == &o_LOR) {
        Obj *result = truth(&v1->v, TRUTH_BOOL, op->epoint);
        bool i;
        if (result->obj != BOOL_OBJ) return result;
        i = (result == &true_value->v) != (op->op == &o_LOR);
        val_destroy(result);
        if (diagnostics.strict_bool) err_msg_bool_oper(op);
        return val_reference(i ? o2 : &v1->v);
    }
    switch (o2->obj->type) {
    case T_CODE:
        {
            Code *v2 = (Code *)o2;
            err = access_check(v1, op->epoint);
            if (err != NULL) return &err->v;
            err = access_check(v2, op->epoint2);
            if (err != NULL) return &err->v;
            op->v1 = v1->addr;
            op->v2 = v2->addr;
            return op->v1->obj->calc2(op);
        }
    case T_BOOL:
        if (diagnostics.strict_bool) err_msg_bool_oper(op);
        /* fall through */
    case T_INT:
    case T_BITS:
    case T_FLOAT:
    case T_STR:
    case T_BYTES:
    case T_ADDRESS:
        err = access_check(v1, op->epoint);
        if (err != NULL) return &err->v;
        op->v1 = v1->addr;
        switch (op->op->op) {
        case O_ADD:
        case O_SUB:
            {
                ival_t iv;
                err = o2->obj->ival(o2, &iv, 31, op->epoint2);
                if (err != NULL) {
                    op->v1 = &v1->v;
                    return &err->v;
                }
                v = new_code();
                memcpy(((unsigned char *)v) + sizeof(Obj), ((unsigned char *)v1) + sizeof(Obj), sizeof(Code) - sizeof(Obj));
                v->names = ref_namespace(v1->names);
                v->addr = op->v1->obj->calc2(op);
                switch (op->op->op) {
                case O_ADD: v->offs += iv; break;
                case O_SUB: v->offs -= iv; break;
                default: break;
                }
                if (v->offs >= 1073741824) { err_msg2(ERROR__OFFSET_RANGE, NULL, op->epoint2); v->offs = 1073741823; }
                if (v->offs < -1073741824) { err_msg2(ERROR__OFFSET_RANGE, NULL, op->epoint2); v->offs = -1073741824; }
                if (v->addr->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)v->addr); v->addr = (Obj *)ref_none(); }
                return &v->v;
            }
        default: break;
        }
        return op->v1->obj->calc2(op);
    default: 
        return o2->obj->rcalc2(op);
    }
    return obj_oper_error(op);
}

static MUST_CHECK Obj *rcalc2(oper_t op) {
    Code *v2 = (Code *)op->v2, *v;
    Obj *o1 = op->v1;
    Error *err;
    if (op->op == &o_IN) {
        struct oper_s oper;
        address_t ln, ln2;
        size_t i;
        ssize_t offs;
        Obj *tmp, *result;

        if (v2->pass != pass) {
            return (Obj *)new_error(ERROR____NO_FORWARD, op->epoint2);
        }

        ln2 = (v2->dtype < 0) ? (address_t)-v2->dtype : (address_t)v2->dtype;
        if (ln2 == 0) ln2 = 1;
        ln = calc_size(v2) / ln2;

        if (ln == 0) {
            return (Obj *)ref_bool(false_value);
        }

        if (v2->offs >= 0) {
            offs = (ssize_t)(((uval_t)v2->offs + ln2 - 1) / ln2);
        } else {
            offs = -(ssize_t)(((uval_t)-v2->offs + ln2 - 1) / ln2);
        }

        oper.op = &o_EQ;
        oper.epoint = op->epoint;
        oper.epoint2 = op->epoint2;
        oper.epoint3 = op->epoint3;
        for (i = 0; i < ln; i++) {
            tmp = code_item(v2, (ssize_t)i + offs, ln2);
            oper.v1 = tmp;
            oper.v2 = o1;
            result = tmp->obj->calc2(&oper);
            val_destroy(tmp);
            if ((Bool *)result == true_value) return result;
            val_destroy(result);
        }
        return (Obj *)ref_bool(false_value);
    }
    switch (o1->obj->type) {
    case T_CODE:
        {
            Code *v1 = (Code *)o1;
            err = access_check(v1, op->epoint);
            if (err != NULL) return &err->v;
            err = access_check(v2, op->epoint2);
            if (err != NULL) return &err->v;
            op->v1 = v1->addr;
            op->v2 = v2->addr;
            return op->v1->obj->calc2(op);
        }
    case T_BOOL:
        if (diagnostics.strict_bool) err_msg_bool_oper(op);
        /* fall through */
    case T_INT:
    case T_BITS:
    case T_FLOAT:
    case T_ADDRESS:
        err = access_check(v2, op->epoint2);
        if (err != NULL) return &err->v;
        op->v2 = v2->addr;
        if (op->op == &o_ADD) {
            ival_t iv;
            err = o1->obj->ival(o1, &iv, 31, op->epoint);
            if (err != NULL) {
                op->v2 = &v2->v;
                return &err->v;
            }
            v = new_code();
            memcpy(((unsigned char *)v) + sizeof(Obj), ((unsigned char *)v2) + sizeof(Obj), sizeof(Code) - sizeof(Obj));
            v->names = ref_namespace(v2->names);
            v->addr = o1->obj->calc2(op);
            v->offs = v2->offs + iv;
            if (v->offs >= 1073741824) { err_msg2(ERROR__OFFSET_RANGE, NULL, op->epoint2); v->offs = 1073741823; }
            if (v->offs < -1073741824) { err_msg2(ERROR__OFFSET_RANGE, NULL, op->epoint2); v->offs = -1073741824; }
            if (v->addr->obj == ERROR_OBJ) { err_msg_output_and_destroy((Error *)v->addr); v->addr = (Obj *)ref_none(); }
            return &v->v;
        }
        return o1->obj->calc2(op);
    default: break;
    }
    return obj_oper_error(op);
}

void codeobj_init(void) {
    new_type(&obj, T_CODE, "code", sizeof(Code));
    obj.create = create;
    obj.destroy = destroy;
    obj.garbage = garbage;
    obj.same = same;
    obj.truth = truth;
    obj.repr = repr;
    obj.ival = ival;
    obj.uval = uval;
    obj.uval2 = uval2;
    obj.address = address;
    obj.sign = sign;
    obj.function = function;
    obj.len = len;
    obj.size = size;
    obj.calc1 = calc1;
    obj.calc2 = calc2;
    obj.rcalc2 = rcalc2;
    obj.slice = slice;
}

void codeobj_names(void) {
    new_builtin("code", val_reference(&CODE_OBJ->v));
}
